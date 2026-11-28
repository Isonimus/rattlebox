#!/usr/bin/env node
// Device check for presets and the remembered roll (slice 0039, ADR-0038): that a choice survives
// a power cycle, that a hold saves one and a hold recalls it, and that a recall throws the dice.
//
// Resets a board running the product, sets a choice by typing an expression, resets it again and
// checks the boot line brings that choice back. Then it saves a preset, changes the choice with
// the controls, resets once more to show that a change made by button persists too, and recalls
// the slot. Persistence across a reset is the one claim no unit test can make, which is the whole
// reason this script exists; what a record means is `test/test_preset_store`'s to assert.
//
// It fails on any boot failure, error line or panic, a boot that did not restore what was stored,
// a store that reported a read or write failure, a save or recall the board did not report, a
// recall that returned the wrong slot or choice, and a recall that threw nothing.
//
// The captures are the half that needs eyes: the band confirming a save and a recall, which is the
// player's only confirmation that a hold did anything (ADR-0038).
// Writes artifacts/presets-verify/<env>/: summary.json, summary.md and the captures.
//
// Usage: npm run verify:presets -- --env cardputer --port /dev/ttyACM0
// Flash the product first: pio run -e <env> -t upload
//
// Everything above the process handling is pure and tested by scripts/presets-verify.test.mjs.

import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

import { checkBoot, checkNoErrors } from './skeleton-verify.mjs';
import { BOOT_TIMEOUT_MS, ROLL_TIMEOUT_MS, ATTACH_SETTLE_MS, Session, capture } from './roll-verify.mjs';
import { parseNotationLine, SUBMIT } from './notation-verify.mjs';

// Both boards, because the serial port drives the same path the controls do and the store is the
// same NVS on each. The StickC is the board the feature was written for: it cannot type a modifier
// at all, so recalling one is its only route to a modified roll (ADR-0036, ADR-0038).
export const ENVS = ['cardputer', 'm5stickc-plus2'];

// What the product's own bindings are over the serial port (src/app/serial_commands.h). 'T' is
// PRIMARY, which submits a typed expression and otherwise steps the die type; 'H' and 'K' are the
// two holds, which no key sequence can stand in for over a serial port.
export const RECALL = 'H';
export const KEEP = 'K';
export const STEP = 'T';

/**
 * The expression typed to set a distinctive choice.
 *
 * Three dice, a type that is not the boot default, and a modifier, because the modifier is the
 * part of a choice only a typed expression can set and so the part a remembered roll has to carry
 * for the StickC to reach it at all (ADR-0038).
 */
export const TYPED = { expression: '3d6+2', choice: { type: 'D6', count: 3, modifier: 2 } };

/**
 * What PRIMARY makes of the typed choice, so a change made by button can be shown to persist too.
 *
 * PRIMARY steps to the next type in ascending order of outcomes (slice 0026), which from a D6 is
 * the D8, and the modifier rides through a change of type untouched (ADR-0036).
 */
export const STEPPED = { choice: { type: 'D8', count: 3, modifier: 2 } };

const RESTORED = /^\[presets\] restored (D\d+) x(\d) mod=([+-]\d), (\d+) saved$/;
const NOTHING_STORED = /^\[presets\] nothing stored, starting at (D\d+) x(\d) mod=([+-]\d)$/;
const RECORD_REFUSED = /^\[presets\] record refused, starting at (D\d+) x(\d) mod=([+-]\d)$/;
const READ_FAILED = /^\[presets\] read failed: (.+), starting at (D\d+) x(\d) mod=([+-]\d)$/;
const WRITE_FAILED = /^\[presets\] write failed: (.+)$/;
const SAVED = /^\[presets\] saved (\d): (D\d+) x(\d) mod=([+-]\d)$/;
const RECALLED = /^\[presets\] recalled (\d): (D\d+) x(\d) mod=([+-]\d)$/;
const NONE_SAVED = /^\[presets\] recall: nothing saved$/;

/** @returns {{kind: string, [key: string]: unknown}} */
export function parsePresetLine(line) {
  let match;
  if ((match = RESTORED.exec(line))) {
    return {
      kind: 'presets-restored',
      type: match[1],
      count: Number(match[2]),
      modifier: Number(match[3]),
      saved: Number(match[4]),
    };
  }
  if ((match = NOTHING_STORED.exec(line))) {
    return { kind: 'presets-nothing', type: match[1], count: Number(match[2]), modifier: Number(match[3]) };
  }
  if ((match = RECORD_REFUSED.exec(line))) {
    return { kind: 'presets-record-refused', type: match[1], count: Number(match[2]), modifier: Number(match[3]) };
  }
  if ((match = READ_FAILED.exec(line))) return { kind: 'presets-read-failed', reason: match[1] };
  if ((match = WRITE_FAILED.exec(line))) return { kind: 'presets-write-failed', reason: match[1] };
  if ((match = SAVED.exec(line))) {
    return {
      kind: 'presets-saved',
      slot: Number(match[1]),
      type: match[2],
      count: Number(match[3]),
      modifier: Number(match[4]),
    };
  }
  if ((match = RECALLED.exec(line))) {
    return {
      kind: 'presets-recalled',
      slot: Number(match[1]),
      type: match[2],
      count: Number(match[3]),
      modifier: Number(match[4]),
    };
  }
  if (NONE_SAVED.test(line)) return { kind: 'presets-none-saved' };
  // Rolls, typed expressions and the boot lines all still parse, so this reads one log.
  return parseNotationLine(line);
}

/** The kinds this check reasons about, in the order the board prints them. */
export const WATCHED = [
  'presets-restored',
  'presets-nothing',
  'presets-record-refused',
  'presets-saved',
  'presets-recalled',
  'presets-none-saved',
  'typed-accepted',
  'launch',
];

/** A choice read off a line, as a phrase for a failure message. */
function describe(choice) {
  // Signed the way the board prints it with %+d, so a zero reads as +0 in both places.
  const modifier = choice.modifier === undefined ? '' : ` mod ${choice.modifier >= 0 ? '+' : ''}${choice.modifier}`;
  return `${choice.count} ${choice.type}${modifier}`;
}

/**
 * Whether `seen` carries the choice `expected` names.
 *
 * Only the fields the expectation names are compared, because a launch line says the type and the
 * count and not the modifier: the modifier is checked where the board reports a choice in full,
 * and a launch is checked for throwing the right dice.
 */
function sameChoice(seen, expected) {
  return Object.keys(expected).every((field) => seen[field] === expected[field]);
}

/**
 * Every failure in a run's lines, against `plan`: the marks the script expected, in order.
 *
 * The plan is built as the run goes, because which slot a save lands in depends on what the board
 * already had stored and a device that has run before is not an empty one.
 */
export function checkRun(parsed, plan) {
  const failures = [...checkBoot(parsed).failures, ...checkNoErrors(parsed).failures];

  // A storage layer that failed is a failure of the run wherever it appears, not just where the
  // plan was looking: the whole point of the record is that it is there next time (ADR-0038).
  for (const entry of parsed) {
    if (entry.kind === 'presets-read-failed') failures.push(`the store failed to read: ${entry.reason}`);
    if (entry.kind === 'presets-write-failed') failures.push(`the store failed to write: ${entry.reason}`);
    if (entry.kind === 'presets-record-refused') {
      failures.push(`the stored record was refused, so it was written wrong or read wrong`);
    }
  }

  const marks = parsed.filter((entry) => WATCHED.includes(entry.kind));
  if (marks.length !== plan.length) {
    failures.push(`expected ${plan.length} reported steps, saw ${marks.length}`);
  }
  plan.forEach((expected, index) => {
    const seen = marks[index];
    if (seen === undefined) {
      failures.push(`step ${index + 1}: expected ${expected.kind}, and the board reported nothing`);
      return;
    }
    if (seen.kind !== expected.kind) {
      failures.push(`step ${index + 1}: expected ${expected.kind}, the board reported ${seen.kind}`);
      return;
    }
    if (expected.choice !== undefined && !sameChoice(seen, expected.choice)) {
      failures.push(`step ${index + 1} (${expected.kind}): ${describe(seen)}, not ${describe(expected.choice)}`);
    }
    if (expected.slot !== undefined && seen.slot !== expected.slot) {
      failures.push(`step ${index + 1} (${expected.kind}): slot ${seen.slot}, not slot ${expected.slot}`);
    }
  });
  return failures;
}

export function summaryMarkdown(env, steps) {
  const lines = [
    `# Presets and the remembered roll on the ${env}`,
    '',
    'A choice is set, the board is reset, and the boot line says what came back. Then a preset is',
    'saved, the choice is changed by button, the board is reset again, and the preset is recalled.',
    'The captures are the half that needs eyes: the band is the only confirmation a hold gives',
    '(ADR-0038).',
    '',
    '| Step | What the board reported |',
    '|---|---|',
  ];
  for (const step of steps) lines.push(`| ${step.what} | ${step.line} |`);
  return `${lines.join('\n')}\n`;
}

export function parseArgs(argv) {
  const args = { env: null, port: null };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--env') args.env = argv[++i] ?? null;
    else if (argv[i] === '--port') args.port = argv[++i] ?? null;
    else throw new Error(`unknown argument ${argv[i]}`);
  }
  if (!ENVS.includes(args.env)) throw new Error(`--env must be one of ${ENVS.join(', ')}`);
  if (!args.port) throw new Error('--port is required, e.g. --port /dev/ttyACM0');
  return args;
}

// ---------------------------------------------------------------- process handling

// A hold either prints what it did or does nothing at all, within a frame of the byte arriving.
export const REPORT_TIMEOUT_MS = 5000;
// Long enough after a save for a roll it should not have thrown to have been announced: a save
// keeps the choice and must not launch anything.
export const QUIET_MS = 500;

async function main() {
  const { env, port } = parseArgs(process.argv.slice(2));
  const outDir = join('artifacts', 'presets-verify', env);
  mkdirSync(outDir, { recursive: true });

  const sessions = [];
  const steps = [];
  const plan = [];
  let failure = null;

  /** Reset the board and wait for the boot that follows, returning the session it runs in. */
  const rebootInto = async (what) => {
    const session = new Session(port, { reset: true, parse: parsePresetLine });
    sessions.push(session);
    console.log(`  ${what}...`);
    await session.next(['boot-ready'], BOOT_TIMEOUT_MS, `boot (${what})`);
    return session;
  };

  try {
    console.log(`Resetting the ${env} on ${port}...`);
    let session = await rebootInto('first boot');
    // The first boot's store holds whatever this board had before, which on a board that has run
    // the product is not nothing, so the first restore line is read but not insisted on. Every
    // later one is checked against what this run itself wrote.
    const first = session.parsed.find((entry) =>
      ['presets-restored', 'presets-nothing', 'presets-record-refused'].includes(entry.kind),
    );
    if (first === undefined) throw new Error('the first boot said nothing about the store');
    plan.push({ kind: first.kind });
    // setup() throws a roll of its own after every boot, so each boot contributes a launch as well
    // as a store line. The first boot's choice is whatever this board had stored, which is not this
    // run's to predict, so only the launch itself is expected here.
    plan.push({ kind: 'launch' });
    steps.push({ what: 'first boot', line: first.kind });
    await session.next(['frame'], ROLL_TIMEOUT_MS, 'end of the boot roll');

    // Set a distinctive choice by typing it, which is the one way to set a modifier (ADR-0036).
    console.log(`  typing ${TYPED.expression}...`);
    const accepted = session.next(['typed-accepted'], REPORT_TIMEOUT_MS, `a verdict on "${TYPED.expression}"`);
    session.send(TYPED.expression);
    session.send(SUBMIT);
    plan.push({ kind: 'typed-accepted', choice: TYPED.choice });
    plan.push({ kind: 'launch', choice: { type: TYPED.choice.type, count: TYPED.choice.count } });
    steps.push({ what: `typed \`${TYPED.expression}\``, line: JSON.stringify(await accepted) });
    await session.next(['locked'], ROLL_TIMEOUT_MS, 'the roll the expression threw');
    await session.close();

    // The claim no unit test can make: the choice comes back after the power was cut.
    session = await rebootInto('reset, to show the typed choice came back');
    const restored = session.parsed.find((entry) => entry.kind === 'presets-restored');
    if (restored === undefined) throw new Error('the boot after the reset restored nothing');
    plan.push({ kind: 'presets-restored', choice: TYPED.choice });
    // The boot roll throws what was restored, which is a stronger claim than the line alone: the
    // choice reached the tray and not just the log.
    plan.push({ kind: 'launch', choice: { type: TYPED.choice.type, count: TYPED.choice.count } });
    steps.push({ what: 'reset, then boot', line: `restored ${describe(restored)}, ${restored.saved} saved` });
    await session.next(['frame'], ROLL_TIMEOUT_MS, 'end of the boot roll');

    // Save it, which must report a slot and must not throw anything.
    const saved = session.next(['presets-saved'], REPORT_TIMEOUT_MS, 'a save');
    session.send(KEEP);
    const savedAt = await saved;
    plan.push({ kind: 'presets-saved', choice: TYPED.choice, slot: savedAt.slot });
    steps.push({ what: 'held the secondary control', line: `saved as ${savedAt.slot}` });
    await new Promise((resolve) => setTimeout(resolve, QUIET_MS));
    await session.close();
    capture(port, join(outDir, 'saved'));
    session = new Session(port, { reset: false, parse: parsePresetLine });
    sessions.push(session);
    await new Promise((resolve) => setTimeout(resolve, ATTACH_SETTLE_MS));

    // Change the choice with the controls, so a change no expression made is shown to persist too.
    console.log('  stepping the die type with the primary control...');
    session.send(STEP);
    plan.push({ kind: 'launch', choice: { type: STEPPED.choice.type, count: STEPPED.choice.count } });
    await session.next(['locked'], ROLL_TIMEOUT_MS, 'the roll the step threw');
    steps.push({ what: 'pressed the primary control', line: `stepped to ${describe(STEPPED.choice)}` });
    await session.close();

    session = await rebootInto('reset, to show the stepped choice came back');
    const steppedBack = session.parsed.find((entry) => entry.kind === 'presets-restored');
    if (steppedBack === undefined) throw new Error('the boot after the second reset restored nothing');
    plan.push({ kind: 'presets-restored', choice: STEPPED.choice });
    plan.push({ kind: 'launch', choice: { type: STEPPED.choice.type, count: STEPPED.choice.count } });
    steps.push({ what: 'reset, then boot', line: `restored ${describe(steppedBack)}` });
    await session.next(['frame'], ROLL_TIMEOUT_MS, 'end of the boot roll');

    // And the preset is still there, and recalling it throws it.
    const recalled = session.next(['presets-recalled', 'presets-none-saved'], REPORT_TIMEOUT_MS, 'a recall');
    session.send(RECALL);
    const recall = await recalled;
    if (recall.kind === 'presets-none-saved') throw new Error('the recall found nothing, after a save that reported a slot');
    plan.push({ kind: 'presets-recalled', choice: TYPED.choice, slot: savedAt.slot });
    plan.push({ kind: 'launch', choice: { type: TYPED.choice.type, count: TYPED.choice.count } });
    steps.push({ what: 'held the primary control', line: `recalled ${recall.slot}: ${describe(recall)}` });
    await session.next(['locked'], ROLL_TIMEOUT_MS, 'the roll the recall threw');
    await session.close();
    capture(port, join(outDir, 'recalled'));
  } catch (error) {
    failure = error.message;
  }
  await Promise.all(sessions.map((session) => session.close()));
  const parsed = sessions.flatMap((session) => session.parsed);

  const failures = [...(failure === null ? [] : [failure]), ...checkRun(parsed, plan)];
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ env, steps, failures }, null, 2)}\n`);
  writeFileSync(join(outDir, 'summary.md'), summaryMarkdown(env, steps));
  console.log(`\nArtifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${[...new Set(failures)].join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: the choice came back after a reset, by expression and by button, and the');
  console.log('        saved preset was recalled and thrown; compare the captures by eye');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
