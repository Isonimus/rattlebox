#!/usr/bin/env node
// Device check for sound (slice 0013). Interactive: the operator shakes the board and listens.
//
// Resets a board running the product through scripts/serial_reset_monitor.py, checks its boot,
// sets three D20 and then three D6 through the product's serial commands, and prompts for
// SHAKES_PER_TYPE shakes of each, each held still afterwards until the board reports the clacks of
// the dice at rest. Fails on any boot failure, error line or panic, no audio or heap line at boot,
// a roll that never locks or locks without a shake, a shaken roll with no clack, a clack while the
// dice lie at rest, a late frame, or IMU samples coming in under the chip's rate over the rolls.
// Writes artifacts/sound-verify/<env>/: summary.json and summary.md, with the clacks, the longest
// render of a block of samples and the heap of each roll. Whether the clacks sound like dice is
// for the person who shook them to say.
//
// Usage: npm run verify:sound -- --env cardputer --port /dev/ttyACM0
// Flash the product first: pio run -e <env> -t upload
//
// Everything above the process handling is pure and tested by scripts/sound-verify.test.mjs.

import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

import {
  BOOT_CHOICE,
  BOOT_TIMEOUT_MS,
  checkImuRate,
  commandsToReach,
  DICE_PER_ROLL,
  parseArgs,
  pooledImuRateHz,
  ROLL_TIMEOUT_MS,
  Session,
} from './roll-verify.mjs';
import { awaitShake, summarizeShake, TYPES } from './shake-verify.mjs';
import { checkBoot, checkNoErrors } from './skeleton-verify.mjs';

// Two of each type: each shake plays hundreds of clacks, so a few rolls hear every kind of hit.
export const SHAKES_PER_TYPE = 2;

/** One shaken roll's figures, with its clacks, the clacks at rest after it, and its heap. */
export function summarizeSoundShake(type, { shakes, locked, frame, sound, rest, heap }) {
  return {
    ...summarizeShake(type, { shakes, locked, frame }),
    clacks: sound.clacks,
    renderMaxUs: sound.renderMaxUs,
    restClacks: rest.clacks,
    restSeconds: rest.seconds,
    heapFree: heap.free,
    heapMinFree: heap.minFree,
    largestDma: heap.largestDma,
  };
}

/** The heap line boot prints once every part is up: the first heap line before the board is ready. */
export function bootHeap(parsed) {
  const ready = parsed.findIndex((entry) => entry.kind === 'boot-ready');
  const heap = parsed.findIndex((entry) => entry.kind === 'heap');
  return heap === -1 || (ready !== -1 && heap > ready) ? null : parsed[heap];
}

/** Every failure: a failed boot, no sound or heap at boot, a type short of its shakes, or a roll that went wrong. */
export function checkSoundRun(parsed, rolls) {
  const failures = [...checkBoot(parsed).failures, ...checkNoErrors(parsed).failures];
  if (!parsed.some((entry) => entry.kind === 'boot-ok' && entry.subsystem === 'audio')) failures.push('boot: no "[boot] audio OK" line');
  if (bootHeap(parsed) === null) failures.push('boot: no "[heap]" line before the board was ready');
  for (const type of TYPES) {
    const count = rolls.filter((roll) => roll.type === type).length;
    if (count !== SHAKES_PER_TYPE) failures.push(`${type}: ${count} of ${SHAKES_PER_TYPE} shaken rolls locked`);
  }
  rolls.forEach((roll, index) => {
    const label = `roll ${index + 1} (${roll.type})`;
    if (roll.detections === 0) failures.push(`${label}: locked without a shake`);
    if (roll.clacks === 0) failures.push(`${label}: no clack`);
    if (roll.lateFrames > 0) failures.push(`${label}: ${roll.lateFrames} late frames`);
  });
  // Every roll's rest, the boot roll's and the launched ones' as well as the shaken ones'.
  for (const rest of parsed.filter((entry) => entry.kind === 'sound-rest' && entry.clacks > 0)) {
    failures.push(`${rest.clacks} clacks in ${rest.seconds} s at rest after a roll`);
  }
  if (rolls.length > 0) failures.push(...checkImuRate('shaken rolls', pooledImuRateHz(rolls)));
  return failures;
}

export function summaryMarkdown(env, boot, rolls) {
  const lines = [
    `# Sound verify: ${env}`,
    '',
    `${DICE_PER_ROLL} dice a roll, shaken by hand. At boot, with every part up: ${boot === null ? 'no heap line' : `${boot.free} bytes free, the largest DMA block ${boot.largestDma}`}.`,
    'Whether the clacks sound like dice is for the person who shook them to say.',
    '',
    '| Roll | Die | Seconds | Clacks | Clacks at rest | Longest render, us | Lowest free heap | Largest DMA block | Longest frame work, us | Late frames |',
    '|---|---|---|---|---|---|---|---|---|---|',
  ];
  rolls.forEach((roll, index) => {
    lines.push(
      `| ${index + 1} | ${roll.type} | ${roll.seconds.toFixed(2)} | ${roll.clacks} | ${roll.restClacks} in ${roll.restSeconds} s | ${roll.renderMaxUs} | ${roll.heapMinFree} | ${roll.largestDma} | ${roll.workMaxUs} | ${roll.lateFrames} |`,
    );
  });
  return `${lines.join('\n')}\n`;
}

/** Wait for a shaken roll to lock and report its frames, clacks and heap, then its clacks at rest. */
export async function awaitSoundShake(waiters, timeoutMs) {
  // Before anything arrives: the board prints the sound and heap lines right after the frame line.
  const sound = waiters.next(['sound'], timeoutMs, 'clack report');
  const heap = waiters.next(['heap'], timeoutMs, 'heap report');
  const rest = waiters.next(['sound-rest'], timeoutMs, 'clacks at rest');
  Promise.allSettled([sound, heap, rest]);
  const roll = await awaitShake(waiters, timeoutMs);
  const [soundEntry, heapEntry, restEntry] = [await sound, await heap, await rest];
  // A roll started within the rest skips this roll's rest line, so the one that comes is the next
  // roll's: paired with this roll, it would report another roll's rest.
  const between = waiters.parsed.slice(waiters.parsed.indexOf(heapEntry), waiters.parsed.indexOf(restEntry));
  if (between.some((entry) => entry.kind === 'shake' || entry.kind === 'launch')) {
    throw new Error('a new roll started before the 2 s of rest after the lock; hold the board still until it reports them');
  }
  return { ...roll, sound: soundEntry, heap: heapEntry, rest: restEntry };
}

// ---------------------------------------------------------------- process handling

// Time for a person to read the prompt, pick the board up, shake it and hold it still, on top of
// the roll's own.
const SHAKE_TIMEOUT_MS = 60000 + ROLL_TIMEOUT_MS;

async function main() {
  const { env, port } = parseArgs(process.argv.slice(2));
  const outDir = join('artifacts', 'sound-verify', env);
  mkdirSync(outDir, { recursive: true });

  console.log(`Resetting the ${env} on ${port}...`);
  const session = new Session(port, { reset: true });
  const rolls = [];
  let failure = null;
  try {
    await session.next(['boot-ready'], BOOT_TIMEOUT_MS, 'boot');
    // The roll the product launches at boot, and its rest.
    await session.next(['sound-rest'], ROLL_TIMEOUT_MS, 'end of the boot roll');
    let choice = BOOT_CHOICE;
    for (const type of TYPES) {
      const target = { type, count: DICE_PER_ROLL };
      for (const command of commandsToReach(choice, target)) {
        await session.roll(command);
        await session.next(['sound-rest'], ROLL_TIMEOUT_MS, `rest after "${command}"`);
      }
      choice = target;
      for (let shake = 1; shake <= SHAKES_PER_TYPE; shake++) {
        console.log(`\nShake ${shake} of ${SHAKES_PER_TYPE}, ${DICE_PER_ROLL} ${type}: hold the board still for a moment, shake it until`);
        console.log('the dice tumble, listening, then hold it still until the results band shows and 2 s more.');
        const summary = summarizeSoundShake(type, await awaitSoundShake(session.waiters, SHAKE_TIMEOUT_MS));
        rolls.push(summary);
        console.log(`  locked ${summary.results.join(',')}: ${summary.clacks} clacks, ${summary.restClacks} at rest`);
      }
    }
  } catch (error) {
    failure = error.message;
  }
  await session.close();
  const parsed = session.parsed;

  const failures = [...(failure === null ? [] : [failure]), ...checkSoundRun(parsed, rolls)];
  const boot = bootHeap(parsed);
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ env, bootHeap: boot, rolls, failures }, null, 2)}\n`);
  writeFileSync(join(outDir, 'summary.md'), summaryMarkdown(env, boot, rolls));
  console.log(`\nArtifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${[...new Set(failures)].join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: every shake clacked and locked, with silence at rest, no lost IMU samples and no late frames;');
  console.log('say whether the clacks sounded like dice');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
