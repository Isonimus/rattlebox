#!/usr/bin/env node
// Device check for the firmware skeleton (slice 0003).
//
// Resets the board through scripts/serial_reset_monitor.py, checks its boot log, walks the
// operator through the orientation, twist, shake and press checks of the slice's Definition
// of Done, then takes a screen capture. Fails on any boot failure, failed IMU read, ESP-IDF
// error or panic. Writes artifacts/skeleton/<env>/: the raw samples as CSV, a summary with
// the measured rest noise and sample rate, and the screen as PNG.
//
// Usage: npm run verify:skeleton -- --env cardputer-skeleton --port /dev/ttyACM0
// Flash the skeleton first: pio run -e <env> -t upload. The product replaced its readout in
// slice 0011, so the skeleton has envs of its own.
//
// Everything above main() is pure and tested by scripts/skeleton-verify.test.mjs.

import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import { pathToFileURL } from 'node:url';

export const ENVS = ['cardputer-skeleton', 'm5stickc-plus2-skeleton'];
export const REQUIRED_SUBSYSTEMS = ['power', 'display', 'input', 'imu'];

// Thresholds from the slice's Definition of Done, with the accelerometer floor raised from
// +-8 g to the +-16 g ADR-0005 chose: shakes reached 9.8 g.
export const MIN_ACCEL_RANGE_G = 16;
export const MIN_GYRO_RANGE_DPS = 2000;
export const REST_MAGNITUDE_TOLERANCE_G = 0.15;
export const REST_GYRO_LIMIT_DPS = 10;
export const TWIST_PEAK_DPS = 90;
export const SHAKE_PEAK_G = 2;

// A subsystem's name may be words ("front canvas"): the product boots two canvases, and two
// lines both saying "canvas" could not tell which one failed.
const BOOT_OK = /^\[boot\] (\w+(?: \w+)*) OK: (.*)$/;
const BOOT_FAILED = /^\[boot\] (\w+(?: \w+)*) FAILED: (.*)$/;
const BOOT_READY = /^\[boot\] .* ready$/;
const IMU_SAMPLE = /^\[imu\] (\d+)((?: -?\d+(?:\.\d+)?){6})$/;
const INPUT_ACTION = /^\[input\] (\w+)$/;
const IMU_RANGES = /accel \+-(\d+) g gyro \+-(\d+) dps/;
// ESP-IDF's log ("[  305][E][Wire.cpp:520] ..." from Arduino, "E (178) tag: ..." from the
// IDF itself), a panic, and any line our own code marks as failed.
const ERROR_LINES = [/^\[\s*\d+\]\[E\]/, /^E \(\d+\)/, /Guru Meditation|Backtrace:|abort\(\)/, /\bfailed\b/i];

/** @returns {{kind: string, [key: string]: unknown}} */
export function parseLine(line) {
  let match;
  if ((match = BOOT_OK.exec(line))) return { kind: 'boot-ok', subsystem: match[1], detail: match[2] };
  if ((match = BOOT_FAILED.exec(line))) return { kind: 'boot-failed', subsystem: match[1], reason: match[2] };
  if (BOOT_READY.test(line)) return { kind: 'boot-ready' };
  if ((match = IMU_SAMPLE.exec(line))) {
    const [ax, ay, az, gx, gy, gz] = match[2].trim().split(' ').map(Number);
    return { kind: 'sample', sample: { t: Number(match[1]), ax, ay, az, gx, gy, gz } };
  }
  if ((match = INPUT_ACTION.exec(line))) return { kind: 'input', action: match[1] };
  if (ERROR_LINES.some((pattern) => pattern.test(line))) return { kind: 'error', line };
  return { kind: 'other' };
}

/** A check's verdict: `failures` empty means it passed. */
function verdict(failures, measured = {}) {
  return { passed: failures.length === 0, failures, measured };
}

export function checkBoot(parsed) {
  const failures = [];
  const started = new Map(parsed.filter((p) => p.kind === 'boot-ok').map((p) => [p.subsystem, p.detail]));
  for (const failed of parsed.filter((p) => p.kind === 'boot-failed')) {
    failures.push(`boot: ${failed.subsystem} FAILED: ${failed.reason}`);
  }
  for (const subsystem of REQUIRED_SUBSYSTEMS) {
    if (!started.has(subsystem)) failures.push(`boot: no "[boot] ${subsystem} OK" line`);
  }
  if (!parsed.some((p) => p.kind === 'boot-ready')) failures.push('boot: never reported ready');
  const imuDetail = started.get('imu');
  const ranges = imuDetail === undefined ? null : IMU_RANGES.exec(imuDetail);
  if (imuDetail !== undefined && ranges === null) {
    failures.push(`boot: the IMU line does not name its ranges: "${imuDetail}"`);
  }
  if (ranges !== null) {
    const [accelG, gyroDps] = [Number(ranges[1]), Number(ranges[2])];
    if (accelG < MIN_ACCEL_RANGE_G) failures.push(`boot: accel range +-${accelG} g is under +-${MIN_ACCEL_RANGE_G} g`);
    if (gyroDps < MIN_GYRO_RANGE_DPS) failures.push(`boot: gyro range +-${gyroDps} dps is under +-${MIN_GYRO_RANGE_DPS} dps`);
  }
  return verdict(failures, { imu: imuDetail ?? null });
}

export function checkNoErrors(parsed) {
  return verdict(parsed.filter((p) => p.kind === 'error').map((p) => `error line: ${p.line}`));
}

/**
 * A sample of exactly zero on all six axes is an IMU with nothing to report (one not yet
 * converting, or a BMI270 whose config did not load), never a real reading: even at rest,
 * gravity and noise put something on every axis.
 */
export function checkNoZeroSamples(samples) {
  const zeros = samples.filter((s) => [s.ax, s.ay, s.az, s.gx, s.gy, s.gz].every((v) => v === 0));
  return verdict(zeros.length === 0 ? [] : [`${zeros.length} all-zero IMU sample(s), the first at t=${zeros[0].t} us`]);
}

/**
 * The first sample after boot must come from a gyro that is already converting: the
 * MPU6886 read before its start-up time gave exactly zero on all three gyro axes on every
 * one of 150 StickC boots (slice 0003). Only the first sample is checked, because a converting
 * gyro also prints 0.0 on all three axes, though rarely: about once in 5000 Cardputer
 * samples at rest. Lines before the ready line can be the previous run's, cut by the reset.
 */
export function checkFirstSampleSettled(parsed) {
  const ready = parsed.findIndex((p) => p.kind === 'boot-ready');
  const first = ready === -1 ? undefined : parsed.slice(ready + 1).find((p) => p.kind === 'sample');
  if (first === undefined) return verdict(['first sample: none arrived after boot']);
  const { t, gx, gy, gz } = first.sample;
  const notStarted = [gx, gy, gz].every((v) => v === 0);
  return verdict(notStarted ? [`first sample: the gyro reads exactly zero at t=${t} us, so it was read before it started`] : []);
}

const magnitude = (x, y, z) => Math.hypot(x, y, z);
const accelMagnitude = (s) => magnitude(s.ax, s.ay, s.az);
const mean = (values) => values.reduce((sum, v) => sum + v, 0) / values.length;
const stdDev = (values) => {
  const m = mean(values);
  return Math.sqrt(mean(values.map((v) => (v - m) ** 2)));
};

/** The device axis whose mean acceleration is nearest +1 g. */
export function axisNearestPlusOneG(samples) {
  const means = { x: mean(samples.map((s) => s.ax)), y: mean(samples.map((s) => s.ay)), z: mean(samples.map((s) => s.az)) };
  return Object.entries(means).reduce((best, entry) => (Math.abs(entry[1] - 1) < Math.abs(best[1] - 1) ? entry : best))[0];
}

function requireSamples(samples, what) {
  return samples.length === 0 ? [`${what}: no IMU samples arrived`] : [];
}

/** Lying flat, screen up, still: also measures the noise and rate the IMU probe starts from. */
export function checkRest(samples) {
  const failures = requireSamples(samples, 'rest');
  if (failures.length > 0) return verdict(failures);
  const meanMagnitude = mean(samples.map(accelMagnitude));
  if (Math.abs(meanMagnitude - 1) > REST_MAGNITUDE_TOLERANCE_G) {
    failures.push(`rest: |a| is ${meanMagnitude.toFixed(3)} g, more than ${REST_MAGNITUDE_TOLERANCE_G} g from 1 g`);
  }
  const axis = axisNearestPlusOneG(samples);
  if (axis !== 'z') failures.push(`rest: ${axis}, not z, is nearest +1 g`);
  for (const key of ['gx', 'gy', 'gz']) {
    const worst = Math.max(...samples.map((s) => Math.abs(s[key])));
    if (worst > REST_GYRO_LIMIT_DPS) failures.push(`rest: ${key} reached ${worst} dps, over ${REST_GYRO_LIMIT_DPS}`);
  }
  const spanUs = samples[samples.length - 1].t - samples[0].t;
  const measured = {
    meanMagnitudeG: meanMagnitude,
    noiseStdDev: Object.fromEntries(['ax', 'ay', 'az', 'gx', 'gy', 'gz'].map((k) => [k, stdDev(samples.map((s) => s[k]))])),
    sampleRateHz: spanUs > 0 ? ((samples.length - 1) * 1e6) / spanUs : null,
  };
  return verdict(failures, measured);
}

export function checkOrientation(samples, expectedAxis, pose) {
  const failures = requireSamples(samples, pose);
  if (failures.length > 0) return verdict(failures);
  const axis = axisNearestPlusOneG(samples);
  if (axis !== expectedAxis) failures.push(`${pose}: ${axis}, not ${expectedAxis}, is nearest +1 g`);
  return verdict(failures, { nearestAxis: axis });
}

export function checkTwist(samples) {
  const failures = requireSamples(samples, 'twist');
  if (failures.length > 0) return verdict(failures);
  const peak = Math.max(...samples.map((s) => s.gz));
  if (peak <= TWIST_PEAK_DPS) failures.push(`twist: gz peaked at ${peak} dps, not above +${TWIST_PEAK_DPS}`);
  return verdict(failures, { peakGzDps: peak });
}

export function checkShake(samples) {
  const failures = requireSamples(samples, 'shake');
  if (failures.length > 0) return verdict(failures);
  const peak = Math.max(...samples.map(accelMagnitude));
  if (peak <= SHAKE_PEAK_G) failures.push(`shake: |a| peaked at ${peak.toFixed(2)} g, not above ${SHAKE_PEAK_G} g`);
  return verdict(failures, { peakMagnitudeG: peak });
}

/** One press of each: exactly one PRIMARY and one SECONDARY line, nothing else. */
export function checkPresses(actions) {
  const count = (name) => actions.filter((a) => a === name).length;
  const failures = [];
  for (const name of ['PRIMARY', 'SECONDARY']) {
    if (count(name) !== 1) failures.push(`presses: ${count(name)} "[input] ${name}" lines, expected 1`);
  }
  const others = actions.filter((a) => a !== 'PRIMARY' && a !== 'SECONDARY');
  if (others.length > 0) failures.push(`presses: unexpected actions ${others.join(', ')}`);
  return verdict(failures, { actions });
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

const BOOT_TIMEOUT_MS = 15000;
const REST_WINDOW_MS = 3000;
const POSE_WINDOW_MS = 3000;
const MOTION_WINDOW_MS = 5000;
const PRESS_WINDOW_MS = 8000;

const PRESS_KEYS = {
  'cardputer-skeleton': 'the board\'s Enter key, then its Backspace key',
  'm5stickc-plus2-skeleton': 'button A (front), then button B (on the side above the screen\'s board name, not the power button opposite it)',
};

/** Collects the board's lines; every check reads a window of them. */
class BoardLog {
  constructor() {
    this.parsed = [];
    this.samples = [];
    this.waiters = [];
  }

  add(line) {
    const parsed = parseLine(line);
    this.parsed.push(parsed);
    if (parsed.kind === 'sample') this.samples.push({ ...parsed.sample, step: this.step ?? 'boot' });
    if (parsed.kind === 'error') console.error(`  board: ${line}`);
    this.waiters = this.waiters.filter((waiter) => !waiter());
  }

  /** Resolves once `predicate` holds, or rejects after `timeoutMs`. */
  until(predicate, timeoutMs, what) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`timed out after ${timeoutMs} ms waiting for ${what}`)), timeoutMs);
      const waiter = () => {
        if (!predicate()) return false;
        clearTimeout(timer);
        resolve();
        return true;
      };
      if (!waiter()) this.waiters.push(waiter);
    });
  }

  /** Records lines for `ms` while the operator holds a pose, then returns them. */
  async window(step, ms) {
    this.step = step;
    const firstParsed = this.parsed.length;
    const firstSample = this.samples.length;
    await new Promise((resolve) => setTimeout(resolve, ms));
    this.step = null;
    return { parsed: this.parsed.slice(firstParsed), samples: this.samples.slice(firstSample) };
  }
}

async function main() {
  const { env, port } = parseArgs(process.argv.slice(2));
  const outDir = join('artifacts', 'skeleton', env);
  mkdirSync(outDir, { recursive: true });

  const log = new BoardLog();
  const monitor = spawn('python3', ['scripts/serial_reset_monitor.py', '--port', port], { stdio: ['ignore', 'pipe', 'inherit'] });
  monitor.on('exit', (code, signal) => {
    if (signal !== 'SIGTERM') console.error(`serial monitor exited early (code ${code}, signal ${signal})`);
  });
  createInterface({ input: monitor.stdout }).on('line', (line) => log.add(line));

  // The board has an Enter key of its own, so the prompt names the keyboard it reads.
  const prompt = createInterface({ input: process.stdin, output: process.stdout });
  const ask = (question) => new Promise((resolve) => prompt.question(`\n${question}\n  press Enter on this computer when ready `, resolve));
  // readline puts the terminal in raw mode and swallows Ctrl+C, which would leave the run
  // hanging with the serial port held; abort instead.
  prompt.on('SIGINT', () => {
    monitor.kill('SIGTERM');
    console.error('\naborted by the operator');
    process.exit(1);
  });

  // A window is silent for seconds, which reads as a hung run without a word.
  const record = async (step, ms) => {
    console.log(`  recording ${step} for ${ms / 1000} s...`);
    const recorded = await log.window(step, ms);
    console.log('  recorded');
    return recorded;
  };

  const results = {};
  try {
    console.log(`Resetting the ${env} on ${port} and reading its boot log...`);
    await log.until(() => log.parsed.some((p) => p.kind === 'boot-ready' || p.kind === 'boot-failed'), BOOT_TIMEOUT_MS, 'boot');
    results.boot = checkBoot(log.parsed);
    if (results.boot.passed) {
      await ask('Lay the board flat, screen up, and leave it still.');
      results.rest = checkRest((await record('rest', REST_WINDOW_MS)).samples);
      await ask('Hold the board upright, facing you, with the board name on the screen at the top and readable, and keep it still.');
      results.topEdgeUp = checkOrientation((await record('top-edge-up', POSE_WINDOW_MS)).samples, 'y', 'top edge up');
      await ask('Turn it so the side with the circle points at the ceiling (its right edge up), and keep it still.');
      results.rightEdgeUp = checkOrientation((await record('right-edge-up', POSE_WINDOW_MS)).samples, 'x', 'right edge up');
      await ask('Lay it flat, screen up; after Enter, twist it counterclockwise (as seen from the screen) a few times.');
      results.twist = checkTwist((await record('twist', MOTION_WINDOW_MS)).samples);
      await ask('After Enter, shake it hard for a few seconds.');
      results.shake = checkShake((await record('shake', MOTION_WINDOW_MS)).samples);
      await ask(`After Enter, press ${PRESS_KEYS[env]}, once each.`);
      const pressed = (await record('presses', PRESS_WINDOW_MS)).parsed;
      results.presses = checkPresses(pressed.filter((p) => p.kind === 'input').map((p) => p.action));
    }
  } finally {
    prompt.close();
    monitor.kill('SIGTERM');
  }
  results.noErrors = checkNoErrors(log.parsed);
  results.noZeroSamples = checkNoZeroSamples(log.samples);
  results.firstSampleSettled = checkFirstSampleSettled(log.parsed);

  // The capture opens the port without resetting the board, so it shows the live readout.
  await new Promise((resolve) => monitor.once('exit', resolve));
  const capture = spawnSync('python3', ['scripts/capture_screen.py', '--port', port, '--out', outDir], { encoding: 'utf8' });
  results.screenCapture = verdict(capture.status === 0 ? [] : [`screen capture: ${capture.stderr.trim()}`], { png: capture.stdout.trim() });

  const csv = ['step,t_us,ax,ay,az,gx,gy,gz', ...log.samples.map((s) => [s.step, s.t, s.ax, s.ay, s.az, s.gx, s.gy, s.gz].join(','))];
  writeFileSync(join(outDir, 'samples.csv'), `${csv.join('\n')}\n`);
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify(results, null, 2)}\n`);

  const failures = Object.values(results).flatMap((r) => r.failures);
  console.log(`\nArtifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${failures.join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: every skeleton check on the device');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
