import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  awaitRoll,
  checkImuRate,
  checkRun,
  commandsToReach,
  imuRateHz,
  LineWaiters,
  pooledImuRateHz,
  parseArgs,
  parseRollLine,
  ROLLS,
  summarizeType,
  summaryMarkdown,
  TYPE_ORDER,
  dicePerRoll,
} from './roll-verify.mjs';
import { BODIES_PER_DIE, BODY_FACES } from './fairness-probe.mjs';

const BOOT_LINES = [
  '[boot] power OK: HOLD latch on G4',
  '[boot] display OK: ST7789 240x135',
  '[boot] input OK: buttons A (G37) and B (G39)',
  '[boot] imu OK: MPU6886 0x68 accel +-16 g gyro +-2000 dps at 100 Hz',
  '[boot] rng OK: esp_random with the SAR ADC as entropy',
  '[boot] M5StickC Plus2 ready',
];

// 100 samples over 0.99 s: 100 Hz exactly.
const frame = (frames = 30) => ({
  kind: 'frame', frames, workMeanUs: 4000, workMaxUs: 11000, physicsMaxUs: 3000, late: 0, imuSamples: 100, imuSpanUs: 990000, imuGapMaxUs: 10500,
});

/** `rolls` rolls of three dice of `faces` faces, each face in turn, so the counts are as even as they can be. */
function evenRolls(type, faces, rolls) {
  const result = [];
  // One entry per body, which is three for the single-body dice and two for one d% (ADR-0034).
  const dice = dicePerRoll(type);
  const bodies = dice * BODIES_PER_DIE[type];
  for (let roll = 0; roll < rolls; roll++) {
    const results = Array.from({ length: bodies }, (unused, die) => ((roll * bodies + die) % faces) + 1);
    result.push({ locked: { kind: 'locked', type, count: dice, results, shown: results, seconds: 0.5, nudges: 0, detections: 0 }, frame: frame() });
  }
  return result;
}

/**
 * Every type rolled and fair, with `replaced` standing in for the ones a test is about.
 *
 * Built from TYPE_ORDER rather than listed, so a die added to the product does not quietly
 * leave these checking a run that is missing one.
 */
function everyTypeRolledEvenly(replaced = {}) {
  return TYPE_ORDER.map((type) => summarizeType(type, replaced[type] ?? evenRolls(type, BODY_FACES[type], ROLLS[type])));
}

test('the product\'s roll and frame lines parse', () => {
  assert.deepEqual(parseRollLine('[roll] launch D20 x3'), { kind: 'launch', type: 'D20', count: 3 });
  assert.deepEqual(parseRollLine('[roll] shake D6 x3 level_tilt_deg=31.4'), { kind: 'shake', type: 'D6', count: 3, levelTiltDeg: 31.4 });
  assert.deepEqual(parseRollLine('[roll] locked D6 x3 results=2,5,6 shown=2,5,1 seconds=0.467 nudges=2 detections=1'), {
    kind: 'locked', type: 'D6', count: 3, results: [2, 5, 6], shown: [2, 5, 1], seconds: 0.467, nudges: 2, detections: 1,
  });
  assert.deepEqual(
    parseRollLine('[frame] frames=28 work_mean_us=3806 work_max_us=10482 physics_max_us=3245 late=1 imu_samples=47 imu_span_us=460120 imu_gap_max_us=10950'),
    {
      kind: 'frame', frames: 28, workMeanUs: 3806, workMaxUs: 10482, physicsMaxUs: 3245, late: 1, imuSamples: 47, imuSpanUs: 460120, imuGapMaxUs: 10950,
    },
  );
});

test('the product\'s sound and heap lines parse', () => {
  assert.deepEqual(parseRollLine('[sound] clacks=212 render_max_us=175'), { kind: 'sound', clacks: 212, renderMaxUs: 175 });
  assert.deepEqual(parseRollLine('[sound] rest clacks=0 seconds=2.0'), { kind: 'sound-rest', clacks: 0, seconds: 2 });
  assert.deepEqual(parseRollLine('[heap] free=190132 min_free=189980 largest_dma=147444'), {
    kind: 'heap', free: 190132, minFree: 189980, largestDma: 147444,
  });
});

test('a shake before the device was ever held still has no level tilt', () => {
  assert.equal(parseRollLine('[roll] shake D20 x1 level_tilt_deg=none').levelTiltDeg, null);
});

test('the IMU rate comes from the samples and their span, and needs two of them', () => {
  assert.equal(imuRateHz(frame()), 100);
  assert.equal(imuRateHz({ ...frame(), imuSamples: 1, imuSpanUs: 0 }), null);
});

test('the pooled IMU rate weighs each roll by its span, so one roll\'s late read barely moves it', () => {
  // A BMI270's 98.9 Hz (ADR-0005): 60 samples in 0.6 s, and the same roll with its last read 7 ms
  // late, which alone reads 97.8 Hz and would fail a per-roll check.
  const onTime = { ...frame(), imuSamples: 60, imuSpanUs: 596562 };
  const lateRead = { ...onTime, imuSpanUs: onTime.imuSpanUs + 7000 };
  assert.ok(imuRateHz(lateRead) < 98);
  const pooled = pooledImuRateHz([...Array(99).fill(onTime), lateRead]);
  assert.ok(pooled > 98.8 && pooled < 98.9, `pooled ${pooled}`);
  assert.equal(pooledImuRateHz([onTime, { ...frame(), imuSamples: 1, imuSpanUs: 0 }]), null);
});

test('an IMU rate under 98% of 100 Hz fails, and one inside the chips\' clock error passes', () => {
  // 1.1% slow, the worst clock measured (ADR-0005), against one sample in 40 lost.
  assert.deepEqual(checkImuRate('D6 roll 1', 98.9), []);
  assert.match(checkImuRate('D6 roll 1', 97.5)[0], /^D6 roll 1: IMU samples at 97\.5 Hz, under 98% of 100 Hz$/);
  assert.match(checkImuRate('D6 roll 1', null)[0], /fewer than two IMU samples/);
});

test('a locked line whose numerals do not match its die count is refused', () => {
  assert.throws(() => parseRollLine('[roll] locked D6 x3 results=2,5 shown=2,5,6 seconds=0.4 nudges=0 detections=0'), /wants 3 numerals/);
});

// Slice 0035: the count is dice and the numerals are bodies, and for a d% those differ. A log that
// said x2 here and x1 on the launch line would read as a roll that changed size on the way.
test('a percentile line counts one die and two numerals', () => {
  assert.deepEqual(parseRollLine('[roll] locked D100 x1 results=8,4 shown=8,4 seconds=0.5 nudges=0 detections=1'), {
    kind: 'locked', type: 'D100', count: 1, results: [8, 4], shown: [8, 4], seconds: 0.5, nudges: 0, detections: 1,
  });
  // One numeral for a d% is half a roll, and three is a body with no die to belong to.
  assert.throws(() => parseRollLine('[roll] locked D100 x1 results=8 shown=8 seconds=0.5 nudges=0 detections=1'), /wants 2 numerals/);
  assert.throws(() => parseRollLine('[roll] locked D100 x1 results=8,4,1 shown=8,4,1 seconds=0.5 nudges=0 detections=1'), /wants 2 numerals/);
});

test('a roll that never locked is an error line', () => {
  assert.equal(parseRollLine('[roll] FAILED: not every die locked within 20 s').kind, 'error');
});

test('the commands to reach a choice step the type forward through the cycle and cycle the count forward', () => {
  // The cycle is D4, D6, D8, D10, D12, D20, D100 and wraps, so a step back is six steps forward.
  assert.deepEqual(commandsToReach({ type: 'D20', count: 1 }, { type: 'D4', count: 3 }), ['T', 'T', 'C', 'C']);
  assert.deepEqual(commandsToReach({ type: 'D20', count: 1 }, { type: 'D6', count: 3 }), ['T', 'T', 'T', 'C', 'C']);
  assert.deepEqual(commandsToReach({ type: 'D4', count: 3 }, { type: 'D6', count: 3 }), ['T']);
  assert.deepEqual(commandsToReach({ type: 'D6', count: 3 }, { type: 'D8', count: 3 }), ['T']);
  assert.deepEqual(commandsToReach({ type: 'D6', count: 3 }, { type: 'D10', count: 3 }), ['T', 'T']);
  assert.deepEqual(commandsToReach({ type: 'D6', count: 3 }, { type: 'D20', count: 3 }), ['T', 'T', 'T', 'T']);
  // Wrapping round passes through the d%, which holds one die, so the count has to be cycled back
  // up afterwards: the clamp is the device's and the model has to spend the commands it costs.
  assert.deepEqual(commandsToReach({ type: 'D10', count: 3 }, { type: 'D6', count: 3 }), ['T', 'T', 'T', 'T', 'T', 'C', 'C']);
  assert.deepEqual(commandsToReach({ type: 'D20', count: 3 }, { type: 'D8', count: 3 }), ['T', 'T', 'T', 'T', 'C', 'C']);
  // Stepping onto the d% brings the count down to its one die with no command of its own, so no
  // 'C' is sent here and none is needed to come back off it either (slice 0035).
  assert.deepEqual(commandsToReach({ type: 'D20', count: 3 }, { type: 'D100', count: 1 }), ['T']);
  assert.deepEqual(commandsToReach({ type: 'D12', count: 3 }, { type: 'D100', count: 1 }), ['T', 'T']);
  assert.deepEqual(commandsToReach({ type: 'D100', count: 1 }, { type: 'D4', count: 3 }), ['T', 'C', 'C']);
  // Asking for more d% than fit would cycle the count for ever, because SECONDARY holds it at 1.
  assert.throws(() => commandsToReach({ type: 'D100', count: 1 }, { type: 'D100', count: 2 }), /D100 does not roll 2 dice/);
  // A name no die carries, so this keeps meaning "unknown type" as dice are added.
  assert.throws(() => commandsToReach({ type: 'D20', count: 1 }, { type: 'D7', count: 1 }), /no die type D7/);
  assert.deepEqual(commandsToReach({ type: 'D6', count: 3 }, { type: 'D6', count: 2 }), ['C', 'C']);
  assert.deepEqual(commandsToReach({ type: 'D6', count: 2 }, { type: 'D6', count: 2 }), []);
});

test('a type\'s summary counts every result, the ones knocked over after locking, and weighs frames by roll length', () => {
  const rolls = [
    { locked: { results: [1, 2, 3], shown: [1, 2, 4], seconds: 1, nudges: 1 }, frame: { ...frame(10), workMeanUs: 1000 } },
    { locked: { results: [6, 6, 6], shown: [6, 6, 6], seconds: 3, nudges: 0 }, frame: { ...frame(30), workMeanUs: 3000, late: 2, imuSpanUs: 1100000, imuGapMaxUs: 21000 } },
  ];
  const summary = summarizeType('D6', rolls);
  assert.deepEqual(summary.counts, [1, 1, 1, 0, 0, 3]);
  assert.equal(summary.results, 6);
  assert.equal(summary.knockedOver, 1);
  assert.equal(summary.meanSeconds, 2);
  assert.equal(summary.longestSeconds, 3);
  assert.equal(summary.nudges, 1);
  assert.equal(summary.workMeanUs, 2500);
  assert.equal(summary.lateFrames, 2);
  // 99 + 99 intervals over 0.99 s + 1.1 s.
  assert.ok(Math.abs(summary.imuRateHz - 198 / 2.09) < 1e-9);
  assert.equal(summary.imuRateMinHz, 90);
  assert.equal(summary.imuGapMaxUs, 21000);
});

test('a run with every roll locked and even results passes', () => {
  const parsed = BOOT_LINES.map(parseRollLine);
  assert.deepEqual(checkRun(parsed, everyTypeRolledEvenly()), []);
});

test('results all on one face fail chi-square, as the host miscompile turned every launch', () => {
  const parsed = BOOT_LINES.map(parseRollLine);
  const stuck = evenRolls('D20', 20, ROLLS.D20).map((roll) => ({ ...roll, locked: { ...roll.locked, results: [7, 7, 7], shown: [7, 7, 7] } }));
  const failures = checkRun(parsed, everyTypeRolledEvenly({ D20: stuck }));
  assert.equal(failures.length, 1);
  assert.match(failures[0], /^D20: chi-square/);
});

test('rolls that lost IMU samples fail their type', () => {
  // One sample in ten lost across every D6 roll.
  const parsed = BOOT_LINES.map(parseRollLine);
  const lossy = evenRolls('D6', 6, ROLLS.D6).map((roll) => ({ ...roll, frame: { ...roll.frame, imuSpanUs: 1100000 } }));
  assert.deepEqual(checkRun(parsed, everyTypeRolledEvenly({ D6: lossy })), ['D6: IMU samples at 90.0 Hz, under 98% of 100 Hz']);
});

test('a type short of its rolls, an error line and a missing entropy source each fail', () => {
  const parsed = [...BOOT_LINES.filter((line) => !line.includes('rng')), '[roll] FAILED: not every die locked within 20 s'].map(parseRollLine);
  const types = [summarizeType('D6', evenRolls('D6', 6, ROLLS.D6 - 1))];
  const failures = checkRun(parsed, types);
  assert.ok(failures.some((line) => /error line: \[roll\] FAILED/.test(line)));
  assert.ok(failures.includes('boot: no "[boot] rng OK" line'));
  assert.ok(failures.includes(`D6: ${ROLLS.D6 - 1} of ${ROLLS.D6} rolls locked`));
  assert.ok(failures.includes(`D20: 0 of ${ROLLS.D20} rolls locked`));
});

test('the markdown has a row per type in both tables', () => {
  const markdown = summaryMarkdown('cardputer', [summarizeType('D6', evenRolls('D6', 6, ROLLS.D6))]);
  assert.match(markdown, /^# Roll verify: cardputer/);
  assert.match(markdown, /\| D6 \| 40 \| 120 \| 0\.00 \| 1\.000 \| 0 \| 0\.50 \/ 0\.50 \| 0 \|/);
  assert.match(markdown, /\| D6 \| 1200 \| 4000 \| 11000 \| 3000 \| 0 \| 100\.0 \| 100\.0 \| 10500 \|/);
});

test('the arguments name a product env and a port', () => {
  assert.deepEqual(parseArgs(['--env', 'cardputer', '--port', '/dev/ttyACM0']), { env: 'cardputer', port: '/dev/ttyACM0' });
  assert.throws(() => parseArgs(['--env', 'cardputer-render-probe', '--port', '/dev/ttyACM0']), /--env must be one of/);
  assert.throws(() => parseArgs(['--env', 'cardputer']), /--port is required/);
  assert.throws(() => parseArgs(['--speed', '9']), /unknown argument/);
});

const LOCKED_LINE = '[roll] locked D6 x3 results=2,5,6 shown=2,5,6 seconds=0.467 nudges=0 detections=0';
const FRAME_LINE = '[frame] frames=28 work_mean_us=3806 work_max_us=10482 physics_max_us=3245 late=0 imu_samples=47 imu_span_us=460120 imu_gap_max_us=10950';
// Long enough for promise continuations, short enough not to slow the suite.
const SHORT_TIMEOUT_MS = 200;

test('a roll whose locked and frame lines arrive together, as one chunk from the monitor, is not missed', async () => {
  const waiters = new LineWaiters();
  // readline hands every line of a chunk over in one synchronous loop, before any await resumes.
  const send = () => {
    waiters.take(LOCKED_LINE);
    waiters.take(FRAME_LINE);
  };
  const roll = await awaitRoll(waiters, send, 'R', SHORT_TIMEOUT_MS);
  assert.deepEqual(roll.locked.results, [2, 5, 6]);
  assert.equal(roll.frame.frames, 28);
});

test('an error line while a roll is awaited fails it at once', async () => {
  const waiters = new LineWaiters();
  const send = () => waiters.take('[roll] FAILED: not every die locked within 20 s');
  await assert.rejects(awaitRoll(waiters, send, 'R', SHORT_TIMEOUT_MS), /board: \[roll\] FAILED/);
});

test('a monitor failure rejects whatever is awaited', async () => {
  const waiters = new LineWaiters();
  const awaited = waiters.next(['locked'], SHORT_TIMEOUT_MS, 'lock');
  waiters.fail(new Error('serial monitor exited early'));
  await assert.rejects(awaited, /exited early/);
});
