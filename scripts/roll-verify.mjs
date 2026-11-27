#!/usr/bin/env node
// Device check for the dice roll (slice 0011), with the IMU's sample rate added in slice 0012 and
// the parsing of the sound and heap lines in slice 0013.
//
// Resets a board running the product through scripts/serial_reset_monitor.py, checks its boot,
// then rolls three of each die type in turn (slices 0026, 0027) through the product's one-byte serial
// commands. Fails on any boot failure, error line or panic, a roll that never locks, a roll whose
// IMU samples came in under the chip's rate, or results that fail chi-square. Chi-square is a coarse check that would
// catch a miscompile like host GCC 13.3's, which turned every launch to the same face (ADR-0009).
// Takes a screen capture after the last roll of each type, for a person to compare each die's face
// up with the results band. Writes artifacts/roll-verify/<env>/:
// summary.json, summary.md and the captures.
//
// Usage: npm run verify:roll -- --env cardputer --port /dev/ttyACM0
// Flash the product first: pio run -e <env> -t upload
//
// Everything above the process handling is pure and tested by scripts/roll-verify.test.mjs.

import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import { pathToFileURL } from 'node:url';

import { ALPHA, BODIES_PER_DIE, BODY_FACES, chiSquarePValue, chiSquareStatistic } from './fairness-probe.mjs';
import { checkBoot, checkNoErrors, parseLine } from './skeleton-verify.mjs';

export const ENVS = ['cardputer', 'm5stickc-plus2'];
// Three dice, the most a roll throws, so every die-to-die contact the physics has is exercised.
// A d% is two bodies, so only one of it fits and it rolls alone (ADR-0034).
export const DICE_PER_ROLL = 3;
// Enough that each face expects at least 5 results, where chi-square's approximation holds:
// 120 D4 results expect 30 a face, 120 D6 results 20, 120 D8 results 15, 120 D10 results 12,
// 120 D12 results 10, 180 D20 results 9 and 80 D100 body results 8. The d% is checked over its
// bodies' ten faces and not over its hundred outcomes: 40 rolls expect 0.4 results an outcome,
// which chi-square cannot read at all. Its hundred outcomes are proven on the host instead, at
// 163,000 rolls (slice 0035); what the device has to show is that every roll locks and that the
// band reads as the dice on the tray do.
// Rolled in this order, which is the product's, so each type is one 'T' from the last.
export const ROLLS = { D4: 40, D6: 40, D8: 40, D10: 40, D12: 40, D20: 60, D100: 40 };
// The order PRIMARY steps the type in (src/roll/die_catalog.h, slices 0026, 0032 and 0035).
export const TYPE_ORDER = ['D4', 'D6', 'D8', 'D10', 'D12', 'D20', 'D100'];
// The product's choice at boot (src/roll/dice_choice.h).
export const BOOT_CHOICE = { type: 'D20', count: 1 };
// The bodies the tray holds (MAX_DICE, src/physics/world.h). This table, TYPE_ORDER and ROLLS are
// spelled out rather than read from the firmware because this check runs against a board over a
// serial port, where no compiler need be; `roll-verify.test.mjs` fails if any of them disagrees
// with the catalog the firmware compiled (ADR-0034).
export const MAX_DICE = 3;

/** The most dice of `type` a roll may throw: MAX_DICE bodies, `BODIES_PER_DIE` at a time. */
export function maxDiceOf(type) {
  return Math.floor(MAX_DICE / BODIES_PER_DIE[type]);
}

/** How many dice of `type` this check rolls: three, or as many as fit. */
export function dicePerRoll(type) {
  return Math.min(DICE_PER_ROLL, maxDiceOf(type));
}
// The product's IMU rate (src/main.cpp, ADR-0005). The chips' clocks were measured up to 1.1% off
// it, so samples coming in under 98% of it were lost between the loop's reads. The rate is pooled
// over a run's rolls: a sample's timestamp is its read time, late by up to a few milliseconds, which
// moved one 0.6 s roll's own rate by about 1% on the Cardputer, whose chip runs 1.1% slow (slice 0012).
export const IMU_RATE_HZ = 100;
export const MIN_IMU_RATE_SHARE = 0.98;
const US_PER_S = 1e6;

const LAUNCH = /^\[roll\] launch (D\d+) x(\d)$/;
const SHAKE = /^\[roll\] shake (D\d+) x(\d) level_tilt_deg=(\d+\.\d|none)$/;
const LOCKED =
  /^\[roll\] locked (D\d+) x(\d) results=([\d,]+) shown=([\d,]+) seconds=(\d+\.\d+) nudges=(\d+) detections=(\d+)$/;
const FRAME =
  /^\[frame\] frames=(\d+) work_mean_us=(\d+) work_max_us=(\d+) physics_max_us=(\d+) late=(\d+) imu_samples=(\d+) imu_span_us=(\d+) imu_gap_max_us=(\d+)$/;

// Slice 0013: each roll's clacks after its frame line, the clacks at rest after its lock, and the heap.
const SOUND = /^\[sound\] clacks=(\d+) render_max_us=(\d+)$/;
const SOUND_REST = /^\[sound\] rest clacks=(\d+) seconds=(\d+\.\d)$/;
const HEAP = /^\[heap\] free=(\d+) min_free=(\d+) largest_dma=(\d+)$/;

const numerals = (field) => field.split(',').map(Number);

/** @returns {{kind: string, [key: string]: unknown}} */
export function parseRollLine(line) {
  let match;
  if ((match = LAUNCH.exec(line))) return { kind: 'launch', type: match[1], count: Number(match[2]) };
  if ((match = SHAKE.exec(line))) {
    // "none" until the device has been held still once since boot: the tray is then level.
    const levelTiltDeg = match[3] === 'none' ? null : Number(match[3]);
    return { kind: 'shake', type: match[1], count: Number(match[2]), levelTiltDeg };
  }
  if ((match = LOCKED.exec(line))) {
    const [results, shown] = [numerals(match[3]), numerals(match[4])];
    const count = Number(match[2]);
    // The count is dice and the numerals are bodies, which differ for a d%: one die, two numerals
    // (ADR-0034). A type this side does not know is a firmware ahead of this script, not a roll to
    // measure by guesswork.
    const bodies = BODIES_PER_DIE[match[1]];
    if (bodies === undefined) throw new Error(`no die type ${match[1]}: ${line}`);
    const expected = count * bodies;
    if (results.length !== expected || shown.length !== expected) {
      throw new Error(`roll of ${count} ${match[1]} wants ${expected} numerals: ${line}`);
    }
    const [seconds, nudges, detections] = match.slice(5).map(Number);
    return { kind: 'locked', type: match[1], count, results, shown, seconds, nudges, detections };
  }
  if ((match = FRAME.exec(line))) {
    const [frames, workMeanUs, workMaxUs, physicsMaxUs, late, imuSamples, imuSpanUs, imuGapMaxUs] = match.slice(1).map(Number);
    return { kind: 'frame', frames, workMeanUs, workMaxUs, physicsMaxUs, late, imuSamples, imuSpanUs, imuGapMaxUs };
  }
  if ((match = SOUND.exec(line))) return { kind: 'sound', clacks: Number(match[1]), renderMaxUs: Number(match[2]) };
  if ((match = SOUND_REST.exec(line))) return { kind: 'sound-rest', clacks: Number(match[1]), seconds: Number(match[2]) };
  if ((match = HEAP.exec(line))) {
    const [free, minFree, largestDma] = match.slice(1).map(Number);
    return { kind: 'heap', free, minFree, largestDma };
  }
  return parseLine(line);
}

/** The IMU's sample rate over one roll, from its samples' timestamps; null with fewer than two samples. */
export function imuRateHz(frame) {
  if (frame.imuSamples < 2 || frame.imuSpanUs === 0) return null;
  return ((frame.imuSamples - 1) * US_PER_S) / frame.imuSpanUs;
}

/** The IMU's rate over many rolls, from their samples and spans together; null if any roll had fewer than two samples. */
export function pooledImuRateHz(frames) {
  if (frames.some((frame) => imuRateHz(frame) === null)) return null;
  const intervals = frames.reduce((sum, frame) => sum + frame.imuSamples - 1, 0);
  const spanUs = frames.reduce((sum, frame) => sum + frame.imuSpanUs, 0);
  return (intervals * US_PER_S) / spanUs;
}

/** A failure for an IMU rate slower than the chip's clock error allows, or for no rate at all. */
export function checkImuRate(label, rate) {
  if (rate === null) return [`${label}: fewer than two IMU samples`];
  if (rate < IMU_RATE_HZ * MIN_IMU_RATE_SHARE) {
    return [`${label}: IMU samples at ${rate.toFixed(1)} Hz, under ${MIN_IMU_RATE_SHARE * 100}% of ${IMU_RATE_HZ} Hz`];
  }
  return [];
}

/** The bytes that take the product from one choice to another: 'T' steps the type, 'C' cycles the count. */
export function commandsToReach(current, target) {
  const from = TYPE_ORDER.indexOf(current.type);
  const to = TYPE_ORDER.indexOf(target.type);
  if (from === -1 || to === -1) throw new Error(`no die type ${from === -1 ? current.type : target.type}`);
  const most = maxDiceOf(target.type);
  if (target.count < 1 || target.count > most) throw new Error(`${target.type} does not roll ${target.count} dice`);
  const commands = [];
  let type = from;
  let count = current.count;
  while (type !== to) {
    type = (type + 1) % TYPE_ORDER.length;
    commands.push('T');
    // Stepping onto a type that holds fewer dice brings the count down with it (ADR-0034), so the
    // device's count and this model of it stay in step without a command being sent.
    count = Math.min(count, maxDiceOf(TYPE_ORDER[type]));
  }
  for (; count !== target.count; count = (count % most) + 1) commands.push('C');
  return commands;
}

/** Each die type's measured rolls added up: `rolls` holds `{locked, frame}` pairs. */
export function summarizeType(type, rolls) {
  const counts = new Array(BODY_FACES[type]).fill(0);
  let knockedOver = 0;
  for (const { locked } of rolls) {
    locked.results.forEach((numeral, die) => {
      counts[numeral - 1]++;
      if (numeral !== locked.shown[die]) knockedOver++;
    });
  }
  const statistic = chiSquareStatistic(counts);
  const frames = rolls.reduce((sum, { frame }) => sum + frame.frames, 0);
  return {
    type,
    rolls: rolls.length,
    results: counts.reduce((sum, count) => sum + count, 0),
    counts,
    chiSquare: statistic,
    pValue: chiSquarePValue(statistic, BODY_FACES[type] - 1),
    knockedOver,
    meanSeconds: rolls.reduce((sum, { locked }) => sum + locked.seconds, 0) / rolls.length,
    longestSeconds: Math.max(...rolls.map(({ locked }) => locked.seconds)),
    nudges: rolls.reduce((sum, { locked }) => sum + locked.nudges, 0),
    frames,
    // Weighted by frames, so a long roll counts for its length.
    workMeanUs: Math.round(rolls.reduce((sum, { frame }) => sum + frame.workMeanUs * frame.frames, 0) / frames),
    workMaxUs: Math.max(...rolls.map(({ frame }) => frame.workMaxUs)),
    physicsMaxUs: Math.max(...rolls.map(({ frame }) => frame.physicsMaxUs)),
    lateFrames: rolls.reduce((sum, { frame }) => sum + frame.late, 0),
    imuRateHz: pooledImuRateHz(rolls.map(({ frame }) => frame)),
    // For reading only: one roll's rate carries its first and last reads' delays (see MIN_IMU_RATE_SHARE).
    imuRateMinHz: rolls.some(({ frame }) => imuRateHz(frame) === null) ? null : Math.min(...rolls.map(({ frame }) => imuRateHz(frame))),
    imuGapMaxUs: Math.max(...rolls.map(({ frame }) => frame.imuGapMaxUs)),
  };
}

/** Every failure: a failed boot, an error line, a type short of its rolls, a roll that lost IMU samples, or results that fail chi-square. */
export function checkRun(parsed, types) {
  const failures = [...checkBoot(parsed).failures, ...checkNoErrors(parsed).failures];
  if (!parsed.some((entry) => entry.kind === 'boot-ok' && entry.subsystem === 'rng')) failures.push('boot: no "[boot] rng OK" line');
  for (const [type, expected] of Object.entries(ROLLS)) {
    const summary = types.find((entry) => entry.type === type);
    if (summary === undefined || summary.rolls !== expected) {
      failures.push(`${type}: ${summary?.rolls ?? 0} of ${expected} rolls locked`);
      continue;
    }
    failures.push(...checkImuRate(type, summary.imuRateHz));
    if (!(summary.pValue >= ALPHA)) {
      failures.push(`${type}: chi-square ${summary.chiSquare.toFixed(2)}, p ${summary.pValue.toFixed(4)} is under ${ALPHA}`);
    }
  }
  return failures;
}

export function summaryMarkdown(env, types) {
  const lines = [
    `# Roll verify: ${env}`,
    '',
    `${DICE_PER_ROLL} dice a roll where they fit, launched from rest through the serial port. A d% is two bodies, so one of it rolls alone; its chi-square is over its bodies' ten faces, not its hundred outcomes (slice 0035).`,
    '',
    '| Die | Rolls | Results | Chi-square | p | Knocked over after locking | Seconds (mean / longest) | Nudges |',
    '|---|---|---|---|---|---|---|---|',
  ];
  for (const t of types) {
    lines.push(`| ${t.type} | ${t.rolls} | ${t.results} | ${t.chiSquare.toFixed(2)} | ${t.pValue.toFixed(3)} | ${t.knockedOver} | ${t.meanSeconds.toFixed(2)} / ${t.longestSeconds.toFixed(2)} | ${t.nudges} |`);
  }
  lines.push('', 'Frame costs while rolling, in microseconds; a frame has 16,667.', '');
  lines.push(
    '| Die | Frames | Work, mean | Work, longest | Four physics steps, longest | Late frames | IMU Hz, all rolls | IMU Hz, slowest roll | Longest IMU gap |',
    '|---|---|---|---|---|---|---|---|---|',
  );
  for (const t of types) {
    const [pooled, slowest] = [t.imuRateHz, t.imuRateMinHz].map((rate) => (rate === null ? 'none' : rate.toFixed(1)));
    lines.push(`| ${t.type} | ${t.frames} | ${t.workMeanUs} | ${t.workMaxUs} | ${t.physicsMaxUs} | ${t.lateFrames} | ${pooled} | ${slowest} | ${t.imuGapMaxUs} |`);
  }
  return `${lines.join('\n')}\n`;
}

/** The board's parsed lines as they arrive, and promises for the next line of a kind. */
export class LineWaiters {
  constructor() {
    this.parsed = [];
    this.waiters = [];
  }

  take(line) {
    let entry;
    try {
      entry = parseRollLine(line);
    } catch (error) {
      this.fail(error);
      return;
    }
    this.parsed.push(entry);
    if (entry.kind === 'error') console.error(`  board: ${line}`);
    for (const waiter of [...this.waiters]) {
      if (entry.kind === 'error' || entry.kind === 'boot-failed') waiter.reject(new Error(`board: ${line}`));
      else if (waiter.kinds.includes(entry.kind)) waiter.resolve(entry);
      else continue;
      this.waiters.splice(this.waiters.indexOf(waiter), 1);
      clearTimeout(waiter.timer);
    }
  }

  fail(error) {
    for (const waiter of this.waiters.splice(0)) {
      clearTimeout(waiter.timer);
      waiter.reject(error);
    }
  }

  /** The next entry of one of `kinds`; rejects on an error line, a failed boot or the timeout. */
  next(kinds, timeoutMs, what) {
    return new Promise((resolve, reject) => {
      const waiter = { kinds, resolve, reject };
      waiter.timer = setTimeout(() => {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        reject(new Error(`no ${what} within ${timeoutMs / 1000} s`));
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }
}

/** Send one command through `send` and wait for the roll it starts to lock and report its frames. */
export async function awaitRoll(waiters, send, command, timeoutMs) {
  // Both before sending: the board prints the two lines back to back, and readline hands over every
  // line of a chunk before an await resumes, so a waiter registered after the first would miss the second.
  const locked = waiters.next(['locked'], timeoutMs, `lock after "${command}"`);
  const frame = waiters.next(['frame'], timeoutMs, 'frame report');
  // Whichever settles first, the other must not go unhandled.
  Promise.allSettled([locked, frame]);
  send(command);
  return { locked: await locked, frame: await frame };
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

export const BOOT_TIMEOUT_MS = 15000;
// The firmware reports a roll that has not locked after 20 s of physics (src/main.cpp); at 60 fps
// that is 20 s of wall time, so this leaves room only for slow frames, not for a hung board.
export const ROLL_TIMEOUT_MS = 30000;
// The monitor needs a moment after the port opens before a byte it forwards reaches the board.
export const ATTACH_SETTLE_MS = 500;

/** One serial session: a monitor process whose lines feed `waiters`, and a way to send the board bytes. */
export class Session {
  constructor(port, { reset }) {
    this.waiters = new LineWaiters();
    this.closing = false;
    const args = ['scripts/serial_reset_monitor.py', '--port', port, '--send-stdin', ...(reset ? [] : ['--no-reset'])];
    this.monitor = spawn('python3', args, { stdio: ['pipe', 'pipe', 'inherit'] });
    this.exited = new Promise((resolve) => this.monitor.once('exit', resolve));
    this.monitor.on('error', (error) => this.waiters.fail(new Error(`could not start the serial monitor: ${error.message}`)));
    // A write to a monitor that has died: without a listener Node throws it past main()'s summary.
    this.monitor.stdin.on('error', (error) => this.waiters.fail(new Error(`serial monitor stdin: ${error.message}`)));
    this.monitor.once('exit', (code, signal) => {
      if (!this.closing) this.waiters.fail(new Error(`serial monitor exited early (code ${code}, signal ${signal})`));
    });
    createInterface({ input: this.monitor.stdout }).on('line', (line) => this.waiters.take(line));
  }

  get parsed() {
    return this.waiters.parsed;
  }

  next(kinds, timeoutMs, what) {
    return this.waiters.next(kinds, timeoutMs, what);
  }

  roll(command) {
    return awaitRoll(this.waiters, (byte) => this.monitor.stdin.write(byte), command, ROLL_TIMEOUT_MS);
  }

  async close() {
    this.closing = true;
    this.monitor.kill('SIGTERM');
    await this.exited;
  }
}

export function capture(port, outDir) {
  mkdirSync(outDir, { recursive: true });
  const result = spawnSync('python3', ['scripts/capture_screen.py', '--port', port, '--out', outDir], { encoding: 'utf8' });
  if (result.status !== 0) throw new Error(`screen capture: ${result.stderr.trim()}`);
}

/** Roll `type` as many at a time as fit, `ROLLS[type]` times from `choice`, and return the rolls and the choice left. */
async function rollType(session, choice, type) {
  const target = { type, count: dicePerRoll(type) };
  for (const command of commandsToReach(choice, target)) await session.roll(command);
  console.log(`  rolling ${ROLLS[type]} times ${target.count} ${type}...`);
  const rolls = [];
  for (let i = 0; i < ROLLS[type]; i++) rolls.push(await session.roll('R'));
  return { rolls, choice: target };
}

async function main() {
  const { env, port } = parseArgs(process.argv.slice(2));
  const outDir = join('artifacts', 'roll-verify', env);
  mkdirSync(outDir, { recursive: true });

  console.log(`Resetting the ${env} on ${port}...`);
  const sessions = [new Session(port, { reset: true })];
  const types = [];
  let failure = null;
  try {
    let session = sessions[0];
    await session.next(['boot-ready'], BOOT_TIMEOUT_MS, 'boot');
    // The roll the product launches at boot.
    await session.next(['frame'], ROLL_TIMEOUT_MS, 'end of the boot roll');
    let choice = BOOT_CHOICE;
    for (const type of Object.keys(ROLLS)) {
      const rolled = await rollType(session, choice, type);
      choice = rolled.choice;
      types.push(summarizeType(type, rolled.rolls));
      await session.close();
      // The board holds the last roll's frame, band included, while the capture has the port.
      capture(port, join(outDir, type));
      session = new Session(port, { reset: false });
      sessions.push(session);
      await new Promise((resolve) => setTimeout(resolve, ATTACH_SETTLE_MS));
    }
  } catch (error) {
    failure = error.message;
  }
  // Closing a session that already ended only waits on its exit, which has happened.
  await Promise.all(sessions.map((session) => session.close()));
  const parsed = sessions.flatMap((session) => session.parsed);

  const failures = [...(failure === null ? [] : [failure]), ...checkRun(parsed, types)];
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ env, types, failures }, null, 2)}\n`);
  writeFileSync(join(outDir, 'summary.md'), summaryMarkdown(env, types));
  console.log(`\nArtifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${[...new Set(failures)].join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: every roll locked and every type passes chi-square; compare the captures with the band by eye');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
