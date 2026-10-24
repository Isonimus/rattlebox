import assert from 'node:assert/strict';
import { test } from 'node:test';

import { LineWaiters, parseRollLine } from './roll-verify.mjs';
import { awaitShake, checkShakeRun, SHAKES_PER_TYPE, summarizeShake, summaryMarkdown, TYPES } from './shake-verify.mjs';

const BOOT_LINES = [
  '[boot] power OK: HOLD latch on G4',
  '[boot] display OK: ST7789 240x135',
  '[boot] input OK: buttons A (G37) and B (G39)',
  '[boot] imu OK: MPU6886 0x68 accel +-16 g gyro +-2000 dps at 100 Hz',
  '[boot] rng OK: esp_random with the SAR ADC as entropy',
  '[boot] M5StickC Plus2 ready',
];

const SHAKE_LINE = '[roll] shake D20 x3 level_tilt_deg=28.6';
const LOCKED_LINE = '[roll] locked D20 x3 results=4,17,9 shown=4,17,9 seconds=3.104 nudges=0 detections=1';
// 311 samples over 3.1 s: 100 Hz exactly.
const FRAME_LINE = '[frame] frames=186 work_mean_us=4200 work_max_us=11800 physics_max_us=4100 late=0 imu_samples=311 imu_span_us=3100000 imu_gap_max_us=10900';

const roll = (type, overrides = {}) => ({
  type, detections: 1, shakeLines: 1, levelTiltDeg: 28.6, seconds: 3.1, results: [4, 17, 9], shown: [4, 17, 9], imuRateHz: 100, imuSamples: 311, imuSpanUs: 3100000, imuGapMaxUs: 10900, workMaxUs: 11800, lateFrames: 0, ...overrides,
});
const fullRun = () => TYPES.flatMap((type) => Array.from({ length: SHAKES_PER_TYPE }, () => roll(type)));

test('a shaken roll is summed up from its shake, locked and frame lines', () => {
  const summary = summarizeShake('D20', {
    shakes: [parseRollLine(SHAKE_LINE), parseRollLine('[roll] shake D20 x3 level_tilt_deg=28.6')],
    locked: { ...parseRollLine(LOCKED_LINE), detections: 2 },
    frame: parseRollLine(FRAME_LINE),
  });
  assert.deepEqual(summary, roll('D20', { detections: 2, shakeLines: 2, seconds: 3.104 }));
});

test('a roll with no level yet keeps its tilt as none', () => {
  const summary = summarizeShake('D6', {
    shakes: [parseRollLine('[roll] shake D6 x3 level_tilt_deg=none')], locked: parseRollLine(LOCKED_LINE), frame: parseRollLine(FRAME_LINE),
  });
  assert.equal(summary.levelTiltDeg, null);
});

test('a run with every shake rolled and locked cleanly passes', () => {
  assert.deepEqual(checkShakeRun(BOOT_LINES.map(parseRollLine), fullRun()), []);
});

test('a roll locked without a shake, short of its shake lines or running late each fail', () => {
  const rolls = fullRun();
  rolls[0] = roll('D20', { detections: 0, shakeLines: 0 });
  rolls[1] = roll('D20', { detections: 2 });
  rolls[3] = roll('D6', { lateFrames: 2 });
  assert.deepEqual(checkShakeRun(BOOT_LINES.map(parseRollLine), rolls), [
    'roll 1 (D20): locked without a shake',
    'roll 2 (D20): 1 shake lines for 2 detections',
    'roll 4 (D6): 2 late frames',
  ]);
});

test('samples lost over the shaken rolls fail them, pooled', () => {
  // One sample in twenty lost from every roll: 295 of 311.
  const lossy = fullRun().map((entry) => ({ ...entry, imuSamples: 295 }));
  assert.deepEqual(checkShakeRun(BOOT_LINES.map(parseRollLine), lossy), ['shaken rolls: IMU samples at 94.8 Hz, under 98% of 100 Hz']);
});

test('a type short of its shakes, an error line and a missing entropy source each fail', () => {
  const parsed = [...BOOT_LINES.filter((line) => !line.includes('rng')), '[imu] FAILED: bus error on a sample read'].map(parseRollLine);
  const failures = checkShakeRun(parsed, fullRun().slice(1));
  assert.ok(failures.some((line) => /error line: \[imu\] FAILED/.test(line)));
  assert.ok(failures.includes('boot: no "[boot] rng OK" line'));
  assert.ok(failures.includes(`D20: ${SHAKES_PER_TYPE - 1} of ${SHAKES_PER_TYPE} shaken rolls locked`));
});

test('the markdown counts the re-detected rolls and has a row per roll', () => {
  const rolls = fullRun();
  rolls[4] = roll('D6', { detections: 2, shakeLines: 2, levelTiltDeg: null });
  const markdown = summaryMarkdown('m5stickc-plus2', rolls);
  assert.match(markdown, /^# Shake verify: m5stickc-plus2/);
  assert.match(markdown, /1 of 6 rolls were detected more than once/);
  assert.match(markdown, /\| 5 \| D6 \| 2 \| none \| 3\.10 \| 4,17,9 \| 4,17,9 \| 100\.0 \| 10900 \| 11800 \| 0 \|/);
});

// Long enough for promise continuations, short enough not to slow the suite.
const SHORT_TIMEOUT_MS = 200;

test('a shaken roll gathers every shake line before its lock, and none from earlier', async () => {
  const waiters = new LineWaiters();
  waiters.take('[roll] shake D20 x3 level_tilt_deg=10.0');
  const awaited = awaitShake(waiters, SHORT_TIMEOUT_MS);
  waiters.take(SHAKE_LINE);
  waiters.take(SHAKE_LINE);
  waiters.take(LOCKED_LINE);
  waiters.take(FRAME_LINE);
  const shaken = await awaited;
  assert.equal(shaken.shakes.length, 2);
  assert.equal(shaken.shakes[0].levelTiltDeg, 28.6);
  assert.equal(shaken.frame.imuSamples, 311);
});

test('an error line while a shake is awaited fails it at once', async () => {
  const waiters = new LineWaiters();
  const awaited = awaitShake(waiters, SHORT_TIMEOUT_MS);
  waiters.take('[imu] FAILED: bus error on a sample read');
  await assert.rejects(awaited, /board: \[imu\] FAILED/);
});
