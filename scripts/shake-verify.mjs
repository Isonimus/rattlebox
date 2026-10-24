#!/usr/bin/env node
// Device check for shake to roll (slice 0012). Interactive: the operator shakes the board.
//
// Resets a board running the product through scripts/serial_reset_monitor.py, checks its boot,
// sets three D20 and then three D6 through the product's serial commands, and prompts for
// SHAKES_PER_TYPE shakes of each. Fails on any boot failure, error line or panic, a roll that
// never locks or locks without a shake, IMU samples coming in under the chip's rate over the
// rolls, or a late frame. Takes a screen capture at each lock, for a person to compare each die's face
// up with the results band, and lists the rolls the detector read more than once, for a person
// to say whether the second kick showed as a stutter (ADR-0010). Writes
// artifacts/shake-verify/<env>/: summary.json, summary.md and the captures.
//
// Usage: npm run verify:shake -- --env cardputer --port /dev/ttyACM0
// Flash the product first: pio run -e <env> -t upload
//
// Everything above the process handling is pure and tested by scripts/shake-verify.test.mjs.

import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

import {
  ATTACH_SETTLE_MS,
  BOOT_CHOICE,
  BOOT_TIMEOUT_MS,
  capture,
  checkImuRate,
  commandsToReach,
  DICE_PER_ROLL,
  imuRateHz,
  parseArgs,
  pooledImuRateHz,
  ROLL_TIMEOUT_MS,
  Session,
} from './roll-verify.mjs';
import { checkBoot, checkNoErrors } from './skeleton-verify.mjs';

// Enough to see each type kicked, tumbled and locked on screen, with a fair chance of a roll the
// detector reads twice: about one in nine on the recorded shakes (ADR-0010).
export const SHAKES_PER_TYPE = 3;
export const TYPES = ['D20', 'D6'];

/** One shaken roll's figures from its shake lines, its locked line and its frame line. */
export function summarizeShake(type, { shakes, locked, frame }) {
  return {
    type,
    detections: locked.detections,
    shakeLines: shakes.length,
    // The level the roll started with: later kicks of the same shake keep it.
    levelTiltDeg: shakes.length === 0 ? null : shakes[0].levelTiltDeg,
    seconds: locked.seconds,
    results: locked.results,
    shown: locked.shown,
    imuRateHz: imuRateHz(frame),
    imuSamples: frame.imuSamples,
    imuSpanUs: frame.imuSpanUs,
    imuGapMaxUs: frame.imuGapMaxUs,
    workMaxUs: frame.workMaxUs,
    lateFrames: frame.late,
  };
}

/** Every failure: a failed boot, no entropy source, an error line, a type short of its shakes, or a roll that went wrong. */
export function checkShakeRun(parsed, rolls) {
  const failures = [...checkBoot(parsed).failures, ...checkNoErrors(parsed).failures];
  // Every kick draws from the RNG.
  if (!parsed.some((entry) => entry.kind === 'boot-ok' && entry.subsystem === 'rng')) failures.push('boot: no "[boot] rng OK" line');
  for (const type of TYPES) {
    const count = rolls.filter((roll) => roll.type === type).length;
    if (count !== SHAKES_PER_TYPE) failures.push(`${type}: ${count} of ${SHAKES_PER_TYPE} shaken rolls locked`);
  }
  rolls.forEach((roll, index) => {
    const label = `roll ${index + 1} (${roll.type})`;
    if (roll.detections === 0) failures.push(`${label}: locked without a shake`);
    if (roll.shakeLines !== roll.detections) failures.push(`${label}: ${roll.shakeLines} shake lines for ${roll.detections} detections`);
    if (roll.lateFrames > 0) failures.push(`${label}: ${roll.lateFrames} late frames`);
  });
  // Pooled, not per roll: one roll's rate carries its first and last reads' delays (roll-verify.mjs).
  if (rolls.length > 0) failures.push(...checkImuRate('shaken rolls', pooledImuRateHz(rolls)));
  return failures;
}

export function summaryMarkdown(env, rolls) {
  const redetected = rolls.filter((roll) => roll.detections > 1).length;
  const lines = [
    `# Shake verify: ${env}`,
    '',
    `${DICE_PER_ROLL} dice a roll, shaken by hand. ${redetected} of ${rolls.length} rolls were detected more than once; whether`,
    'their second kick showed as a stutter is for the person who shook them to say.',
    '',
    '| Roll | Die | Detections | Level tilt, degrees | Seconds | Results | Shown | IMU Hz | Longest IMU gap, us | Longest frame work, us | Late frames |',
    '|---|---|---|---|---|---|---|---|---|---|---|',
  ];
  rolls.forEach((roll, index) => {
    const tilt = roll.levelTiltDeg === null ? 'none' : roll.levelTiltDeg.toFixed(1);
    const rate = roll.imuRateHz === null ? 'none' : roll.imuRateHz.toFixed(1);
    lines.push(
      `| ${index + 1} | ${roll.type} | ${roll.detections} | ${tilt} | ${roll.seconds.toFixed(2)} | ${roll.results.join(',')} | ${roll.shown.join(',')} | ${rate} | ${roll.imuGapMaxUs} | ${roll.workMaxUs} | ${roll.lateFrames} |`,
    );
  });
  return `${lines.join('\n')}\n`;
}

/** Wait for a shaken roll to lock and report its frames; its shake lines are those parsed meanwhile. */
export async function awaitShake(waiters, timeoutMs) {
  const start = waiters.parsed.length;
  // Both before anything arrives: the board prints the two lines back to back (see awaitRoll).
  const locked = waiters.next(['locked'], timeoutMs, 'shaken roll locking');
  const frame = waiters.next(['frame'], timeoutMs, 'frame report');
  Promise.allSettled([locked, frame]);
  const roll = { locked: await locked, frame: await frame };
  return { ...roll, shakes: waiters.parsed.slice(start).filter((entry) => entry.kind === 'shake') };
}

// ---------------------------------------------------------------- process handling

// Time for a person to read the prompt, pick the board up, shake it and hold it still, on top of
// the roll's own.
const SHAKE_TIMEOUT_MS = 60000 + ROLL_TIMEOUT_MS;

async function main() {
  const { env, port } = parseArgs(process.argv.slice(2));
  const outDir = join('artifacts', 'shake-verify', env);
  mkdirSync(outDir, { recursive: true });

  console.log(`Resetting the ${env} on ${port}...`);
  const sessions = [new Session(port, { reset: true })];
  const rolls = [];
  let failure = null;
  try {
    let session = sessions[0];
    await session.next(['boot-ready'], BOOT_TIMEOUT_MS, 'boot');
    // The roll the product launches at boot.
    await session.next(['frame'], ROLL_TIMEOUT_MS, 'end of the boot roll');
    let choice = BOOT_CHOICE;
    for (const type of TYPES) {
      const target = { type, count: DICE_PER_ROLL };
      for (const command of commandsToReach(choice, target)) await session.roll(command);
      choice = target;
      for (let shake = 1; shake <= SHAKES_PER_TYPE; shake++) {
        console.log(
          `\nShake ${shake} of ${SHAKES_PER_TYPE}, ${DICE_PER_ROLL} ${type}: hold the board still at any angle for a moment, shake it`,
        );
        console.log('until the dice tumble, then hold it still until the results band shows and the capture is saved.');
        const roll = await awaitShake(session.waiters, SHAKE_TIMEOUT_MS);
        const summary = summarizeShake(type, roll);
        rolls.push(summary);
        console.log(`  locked ${summary.results.join(',')} after ${summary.detections} detection(s)`);
        await session.close();
        // The board holds the locked frame, band included, while the capture has the port.
        capture(port, join(outDir, `${rolls.length}-${type}`));
        session = new Session(port, { reset: false });
        sessions.push(session);
        await new Promise((resolve) => setTimeout(resolve, ATTACH_SETTLE_MS));
      }
    }
  } catch (error) {
    failure = error.message;
  }
  await Promise.all(sessions.map((session) => session.close()));
  const parsed = sessions.flatMap((session) => session.parsed);

  const failures = [...(failure === null ? [] : [failure]), ...checkShakeRun(parsed, rolls)];
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ env, rolls, failures }, null, 2)}\n`);
  writeFileSync(join(outDir, 'summary.md'), summaryMarkdown(env, rolls));
  console.log(`\nArtifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${[...new Set(failures)].join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: every shake rolled and locked with no lost IMU samples or late frames;');
  console.log('compare the captures with the band by eye, and say whether the re-detected rolls stuttered');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
