#!/usr/bin/env node
// Device check for dice notation (slice 0037, ADR-0036): what the band draws while an expression
// is typed, and that a refused expression throws nothing.
//
// Resets a board running the product through scripts/serial_reset_monitor.py, checks its boot, then
// types each expression below a character at a time through the product's serial port and presses
// Enter. The typed characters are the same commands the keyboard produces, so this drives the path
// a player drives, with nobody at the keyboard. Fails on any boot failure, error line or panic, an
// expression accepted that should be refused or refused that should be accepted, a roll the
// accepted expression did not throw, or a roll thrown after a refusal.
//
// It takes screen captures of the field part way through typing, of the band once a modified roll
// has locked, and of a refusal, because what the band draws is the half of this that needs eyes.
// Writes artifacts/notation-verify/<env>/: summary.json, summary.md and the captures.
//
// Usage: npm run verify:notation -- --env cardputer --port /dev/ttyACM0
// Flash the product first: pio run -e <env> -t upload
//
// Everything above the process handling is pure and tested by scripts/notation-verify.test.mjs.

import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

import { checkBoot, checkNoErrors } from './skeleton-verify.mjs';
import { BOOT_TIMEOUT_MS, ROLL_TIMEOUT_MS, ATTACH_SETTLE_MS, Session, capture, parseRollLine } from './roll-verify.mjs';

// Both boards run the entry, because the serial port types where the keyboard would. Only the
// Cardputer has keys to type with, which is the one step of this no host can drive (slice 0037).
export const ENVS = ['cardputer', 'm5stickc-plus2'];

// What the product's own key bindings are over the serial port (src/app/serial_commands.h):
// 'T' is PRIMARY, which is Enter and so submits, and 'C' is SECONDARY, which deletes a character.
export const SUBMIT = 'T';
export const DELETE = 'C';

/**
 * The expressions typed, in order, and what the firmware must make of each.
 *
 * They cover the grammar's bounds rather than its middle: the modifier the band had to find room
 * for, the die that is two bodies and so fits once, the count the tray cannot hold, and a die that
 * does not exist. The refusals' wording is not checked here, because the firmware aborts rather
 * than draw a reason that does not fit, so a reason the band cannot show fails this run as a
 * panic; what the words are is `test/test_dice_notation`'s to assert (ADR-0036).
 */
export const CASES = [
  { expression: '3d6+2', accepted: true, type: 'D6', count: 3, modifier: 2 },
  { expression: '1d20-9', accepted: true, type: 'D20', count: 1, modifier: -9 },
  { expression: '1d100', accepted: true, type: 'D100', count: 1, modifier: 0 },
  { expression: '4d6', accepted: false },
  { expression: '2d100', accepted: false },
  { expression: '1d7', accepted: false },
];

const ACCEPTED = /^\[roll\] typed (\S+) accepted (D\d+) x(\d) mod=([+-]\d)$/;
const REFUSED = /^\[roll\] typed (\S+) refused: (.+)$/;
const FIELD_FULL = /^\[roll\] typed full: (\S*)$/;

/** @returns {{kind: string, [key: string]: unknown}} */
export function parseNotationLine(line) {
  let match;
  if ((match = ACCEPTED.exec(line))) {
    return {
      kind: 'typed-accepted',
      expression: match[1],
      type: match[2],
      count: Number(match[3]),
      modifier: Number(match[4]),
    };
  }
  if ((match = REFUSED.exec(line))) return { kind: 'typed-refused', expression: match[1], reason: match[2] };
  if ((match = FIELD_FULL.exec(line))) return { kind: 'typed-full', text: match[1] };
  return parseRollLine(line);
}

/** Every failure in a run's lines: the boot, the submissions in order, and what each one threw. */
export function checkRun(parsed) {
  const failures = [...checkBoot(parsed).failures, ...checkNoErrors(parsed).failures];
  const WATCHED = ['typed-accepted', 'typed-refused', 'typed-full', 'launch'];
  const marks = parsed.map((entry, at) => ({ entry, at })).filter(({ entry }) => WATCHED.includes(entry.kind));
  const submissions = marks.filter(({ entry }) => entry.kind !== 'launch');

  if (submissions.length !== CASES.length) {
    failures.push(`expected ${CASES.length} submissions, saw ${submissions.length}`);
  }
  CASES.forEach((expected, index) => {
    const submission = submissions[index];
    if (submission === undefined) return;
    const seen = submission.entry;
    const label = expected.expression;
    if (seen.kind === 'typed-full') {
      failures.push(`${label}: the field filled up at "${seen.text}"`);
      return;
    }
    if (seen.expression !== expected.expression) {
      failures.push(`submission ${index + 1}: the board read "${seen.expression}", not "${expected.expression}"`);
      return;
    }
    // What follows a submission says what it threw: the next launch, if it is still this
    // submission's and not the next one's.
    const next = marks[marks.indexOf(submission) + 1];
    const threw = next !== undefined && next.entry.kind === 'launch' ? next.entry : null;
    if (!expected.accepted) {
      if (seen.kind !== 'typed-refused') failures.push(`${label}: accepted, and it asks for more than the tray holds`);
      else if (seen.reason.length === 0) failures.push(`${label}: refused with no reason to show`);
      // ADR-0036: an expression the tray cannot throw is refused, never reduced to one it can.
      if (threw !== null) failures.push(`${label}: refused and then threw ${threw.count} ${threw.type} anyway`);
      return;
    }
    if (seen.kind !== 'typed-refused') {
      if (seen.type !== expected.type || seen.count !== expected.count || seen.modifier !== expected.modifier) {
        failures.push(
          `${label}: read as ${seen.count} ${seen.type} mod ${seen.modifier}, not ${expected.count} ${expected.type} mod ${expected.modifier}`,
        );
      }
    } else {
      failures.push(`${label}: refused with "${seen.reason}"`);
      return;
    }
    if (threw === null) {
      failures.push(`${label}: accepted and threw nothing`);
    } else if (threw.type !== expected.type || threw.count !== expected.count) {
      failures.push(`${label}: threw ${threw.count} ${threw.type}, not ${expected.count} ${expected.type}`);
    }
  });
  return failures;
}

export function summaryMarkdown(env, submissions) {
  const lines = [
    `# Dice notation on the ${env}`,
    '',
    'Each expression typed through the serial port a character at a time, then Enter. The captures',
    'are the half that needs eyes: the field part way through typing, the band of a modified roll,',
    'and a refusal on screen (ADR-0036).',
    '',
    '| Typed | Verdict | Read as | Threw |',
    '|---|---|---|---|',
  ];
  for (const seen of submissions) {
    if (seen.kind === 'typed-accepted') {
      lines.push(`| \`${seen.expression}\` | accepted | ${seen.count} ${seen.type}, modifier ${seen.modifier} | yes |`);
    } else if (seen.kind === 'typed-refused') {
      lines.push(`| \`${seen.expression}\` | refused | ${seen.reason} | no |`);
    } else {
      lines.push(`| (the field filled up) | full | \`${seen.text}\` | no |`);
    }
  }
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

// A submission either prints its verdict or does nothing at all, and the board prints it within a
// frame of reading the byte. This is long enough to tell a slow board from a silent one.
export const VERDICT_TIMEOUT_MS = 5000;
// After a refusal, long enough for a roll the board should not have thrown to have been announced.
export const QUIET_MS = 500;

/** Type `expression` and press Enter, and return the verdict the board printed. */
async function submit(session, expression) {
  const verdict = session.next(['typed-accepted', 'typed-refused', 'typed-full'], VERDICT_TIMEOUT_MS, `a verdict on "${expression}"`);
  session.send(expression);
  session.send(SUBMIT);
  return verdict;
}

/** Empty a field the board kept after a refusal, one Backspace a character, and no more. */
function clearField(session, expression) {
  // One more would be a SECONDARY on a closed field, which cycles the count and throws a roll.
  for (let at = 0; at < expression.length; at++) session.send(DELETE);
}

async function main() {
  const { env, port } = parseArgs(process.argv.slice(2));
  const outDir = join('artifacts', 'notation-verify', env);
  mkdirSync(outDir, { recursive: true });

  console.log(`Resetting the ${env} on ${port}...`);
  const sessions = [new Session(port, { reset: true, parse: parseNotationLine })];
  const submissions = [];
  let failure = null;
  try {
    let session = sessions[0];
    await session.next(['boot-ready'], BOOT_TIMEOUT_MS, 'boot');
    // The roll the product launches at boot, so the band is not mid-roll when the typing starts.
    await session.next(['frame'], ROLL_TIMEOUT_MS, 'end of the boot roll');

    // The field part way through an expression, before any of it is submitted.
    const held = '3d6+';
    session.send(held);
    await session.close();
    capture(port, join(outDir, 'typing'));
    session = new Session(port, { reset: false, parse: parseNotationLine });
    sessions.push(session);
    await new Promise((resolve) => setTimeout(resolve, ATTACH_SETTLE_MS));
    clearField(session, held);

    for (const expected of CASES) {
      console.log(`  typing ${expected.expression}...`);
      const verdict = await submit(session, expected.expression);
      submissions.push(verdict);
      if (verdict.kind === 'typed-accepted') {
        await session.next(['locked'], ROLL_TIMEOUT_MS, `the roll "${expected.expression}" threw`);
      } else {
        // Nothing should arrive now. The check reads the log for a launch; this only gives one time
        // to appear, so a board that throws late is caught rather than raced past.
        await new Promise((resolve) => setTimeout(resolve, QUIET_MS));
      }
      // The band to look at: a modified roll's results, and a refusal's reason.
      const capturing = expected.expression === CASES[0].expression || !expected.accepted;
      if (capturing) {
        await session.close();
        capture(port, join(outDir, expected.expression.replace(/[+]/g, 'plus').replace(/[-]/g, 'minus')));
        session = new Session(port, { reset: false, parse: parseNotationLine });
        sessions.push(session);
        await new Promise((resolve) => setTimeout(resolve, ATTACH_SETTLE_MS));
      }
      // A refused expression stays in the field on purpose, so the next one starts from empty.
      if (!expected.accepted) clearField(session, expected.expression);
    }
  } catch (error) {
    failure = error.message;
  }
  await Promise.all(sessions.map((session) => session.close()));
  const parsed = sessions.flatMap((session) => session.parsed);

  const failures = [...(failure === null ? [] : [failure]), ...checkRun(parsed)];
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ env, submissions, failures }, null, 2)}\n`);
  writeFileSync(join(outDir, 'summary.md'), summaryMarkdown(env, submissions));
  console.log(`\nArtifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${[...new Set(failures)].join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: every expression was read as it reads, and nothing was thrown for a refusal;');
  console.log('        compare the captures with what was typed by eye');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
