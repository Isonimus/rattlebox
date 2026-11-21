import assert from 'node:assert/strict';
import { test } from 'node:test';

import { LineWaiters, parseRollLine } from './roll-verify.mjs';
import { TYPES } from './shake-verify.mjs';
import { awaitSoundShake, bootHeap, checkSoundRun, SHAKES_PER_TYPE, summarizeSoundShake, summaryMarkdown } from './sound-verify.mjs';

const BOOT_LINES = [
  '[boot] power OK: hardware switch, no latch',
  '[boot] display OK: ST7789 240x135',
  '[boot] input OK: TCA8418 keyboard at 0x34',
  '[boot] imu OK: BMI270 0x69 accel +-16 g gyro +-2000 dps at 100 Hz',
  '[boot] audio OK: ES8311 over I2S, 48 kHz 16-bit stereo',
  '[boot] rng OK: esp_random with the SAR ADC as entropy',
  '[heap] free=190604 min_free=190604 largest_dma=147444',
  '[boot] Cardputer ADV ready',
];
const SHAKE_LINE = '[roll] shake D20 x3 level_tilt_deg=28.6';
const LOCKED_LINE = '[roll] locked D20 x3 results=4,17,9 shown=4,17,9 seconds=3.104 nudges=0 detections=1';
// 311 samples over 3.1 s: 100 Hz exactly.
const FRAME_LINE = '[frame] frames=186 work_mean_us=9000 work_max_us=11800 physics_max_us=4100 late=0 imu_samples=311 imu_span_us=3100000 imu_gap_max_us=10900';
const SOUND_LINE = '[sound] clacks=212 render_max_us=175';
const HEAP_LINE = '[heap] free=190132 min_free=189980 largest_dma=147444';
const REST_LINE = '[sound] rest clacks=0 seconds=2.0';

const roll = (type, overrides = {}) => ({
  ...summarizeSoundShake(type, {
    shakes: [parseRollLine(SHAKE_LINE)],
    locked: parseRollLine(LOCKED_LINE),
    frame: parseRollLine(FRAME_LINE),
    sound: parseRollLine(SOUND_LINE),
    rest: parseRollLine(REST_LINE),
    heap: parseRollLine(HEAP_LINE),
  }),
  ...overrides,
});
const fullRun = () => TYPES.flatMap((type) => Array.from({ length: SHAKES_PER_TYPE }, () => roll(type)));
const bootParsed = () => BOOT_LINES.map(parseRollLine);

test('a shaken roll is summed up with its clacks, its clacks at rest and its heap', () => {
  const summary = roll('D20');
  assert.equal(summary.clacks, 212);
  assert.equal(summary.renderMaxUs, 175);
  assert.equal(summary.restClacks, 0);
  assert.equal(summary.restSeconds, 2);
  assert.deepEqual([summary.heapFree, summary.heapMinFree, summary.largestDma], [190132, 189980, 147444]);
  assert.equal(summary.detections, 1);
});

test('a full run with clacks, silence at rest and a boot heap passes', () => {
  assert.deepEqual(checkSoundRun(bootParsed(), fullRun()), []);
});

test('a board without audio or a heap line at boot fails', () => {
  const parsed = bootParsed().filter((entry) => !(entry.kind === 'boot-ok' && entry.subsystem === 'audio') && entry.kind !== 'heap');
  assert.deepEqual(checkSoundRun(parsed, fullRun()), ['boot: no "[boot] audio OK" line', 'boot: no "[heap]" line before the board was ready']);
});

test('only the heap line before the board is ready is the boot heap', () => {
  const afterReady = [...bootParsed().filter((entry) => entry.kind !== 'heap'), parseRollLine(HEAP_LINE)];
  assert.equal(bootHeap(afterReady), null);
  assert.deepEqual(bootHeap(bootParsed()), parseRollLine(BOOT_LINES[6]));
});

test('a failed audio boot fails the run', () => {
  const parsed = [...bootParsed(), parseRollLine('[boot] audio FAILED: no ACK from the ES8311 codec at 0x18')];
  assert.ok(checkSoundRun(parsed, fullRun()).includes('boot: audio FAILED: no ACK from the ES8311 codec at 0x18'));
});

test('a silent shake and a late frame each fail', () => {
  const rolls = fullRun();
  rolls[0] = { ...rolls[0], clacks: 0 };
  rolls[2] = { ...rolls[2], lateFrames: 2 };
  assert.deepEqual(checkSoundRun(bootParsed(), rolls), ['roll 1 (D20): no clack', 'roll 3 (D6): 2 late frames']);
});

test('a type short of its shakes fails', () => {
  assert.deepEqual(checkSoundRun(bootParsed(), fullRun().slice(1)), [`D20: ${SHAKES_PER_TYPE - 1} of ${SHAKES_PER_TYPE} shaken rolls locked`]);
});

test('the summary table has a row per roll and the boot heap', () => {
  const markdown = summaryMarkdown('cardputer', parseRollLine(BOOT_LINES[6]), fullRun());
  assert.match(markdown, /190604 bytes free, the largest DMA block 147444/);
  assert.equal(markdown.split('\n').filter((line) => /^\| \d/.test(line)).length, fullRun().length);
});

test('a roll\'s sound, heap and rest lines are caught though they follow its frame line at once', async () => {
  const waiters = new LineWaiters();
  const pending = awaitSoundShake(waiters, 1000);
  // One chunk, as the board prints them: every line is handed over before the await resumes.
  for (const line of [SHAKE_LINE, LOCKED_LINE, FRAME_LINE, SOUND_LINE, HEAP_LINE, REST_LINE]) waiters.take(line);
  const result = await pending;
  assert.equal(result.sound.clacks, 212);
  assert.equal(result.heap.largestDma, 147444);
  assert.equal(result.rest.clacks, 0);
  assert.equal(result.shakes.length, 1);
});

test('a rest line after another roll has started is not this roll\'s rest, and fails', async () => {
  // A re-shake within 2 s of the lock starts a roll before this one's rest line prints, so the rest
  // line that follows is the next roll's.
  const waiters = new LineWaiters();
  const pending = awaitSoundShake(waiters, 1000);
  for (const line of [SHAKE_LINE, LOCKED_LINE, FRAME_LINE, SOUND_LINE, HEAP_LINE, SHAKE_LINE, REST_LINE]) waiters.take(line);
  await assert.rejects(pending, /a new roll started before the 2 s of rest after the lock/);
});

test('clacks at rest after any roll fail the run, launched rolls included', () => {
  const parsed = [...bootParsed(), parseRollLine('[sound] rest clacks=3 seconds=2.0')];
  assert.ok(checkSoundRun(parsed, fullRun()).includes('3 clacks in 2 s at rest after a roll'));
});
