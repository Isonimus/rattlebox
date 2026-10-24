// Tests for the pure half of scripts/skeleton-verify.mjs (slice 0003). The boot logs in
// fixtures/skeleton/ were recorded from the boards: the Cardputer ADV's at slice 0003's
// +-8 g, its zero-sample one before the BMI270 driver waited for data-ready, and
// stickc-boot.log at ADR-0005's +-16 g and 100 Hz (slice 0004).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import {
  checkBoot,
  checkNoErrors,
  checkFirstSampleSettled,
  checkNoZeroSamples,
  checkOrientation,
  checkPresses,
  checkRest,
  checkShake,
  checkTwist,
  parseArgs,
  parseLine,
} from './skeleton-verify.mjs';

const fixture = (name) =>
  readFileSync(new URL(`./fixtures/skeleton/${name}`, import.meta.url), 'utf8').split('\n').filter(Boolean).map(parseLine);
const samplesOf = (parsed) => parsed.filter((p) => p.kind === 'sample').map((p) => p.sample);

const SAMPLE_PERIOD_US = 20000;
/** `count` samples 20 ms apart, each from `make(index)`. */
function series(count, make) {
  return Array.from({ length: count }, (_, i) => ({ t: 1_000_000 + i * SAMPLE_PERIOD_US, ...make(i) }));
}
const still = (ax, ay, az) => series(150, () => ({ ax, ay, az, gx: 0.2, gy: -0.3, gz: 0.1 }));

test('a recorded boot at ADR-0005\'s settings passes the boot, error and zero-sample checks', () => {
  const parsed = fixture('stickc-boot.log');
  assert.deepEqual(checkBoot(parsed).failures, []);
  assert.deepEqual(checkNoErrors(parsed).failures, []);
  assert.deepEqual(checkNoZeroSamples(samplesOf(parsed)).failures, []);
});

test('a recorded Cardputer boot passes the error and zero-sample checks', () => {
  const parsed = fixture('cardputer-boot.log');
  assert.deepEqual(checkNoErrors(parsed).failures, []);
  assert.deepEqual(checkNoZeroSamples(samplesOf(parsed)).failures, []);
});

test('the recorded boot that streamed zeros before the first conversion is caught', () => {
  const result = checkNoZeroSamples(samplesOf(fixture('cardputer-boot-zero-samples.log')));
  assert.equal(result.passed, false);
  assert.match(result.failures[0], /^3 all-zero IMU sample\(s\)/);
});

test('a recorded Cardputer boot passes the first-sample check', () => {
  assert.deepEqual(checkFirstSampleSettled(fixture('cardputer-boot.log')).failures, []);
});

test('the recorded StickC boot that read the gyro before it started is caught', () => {
  const result = checkFirstSampleSettled(fixture('stickc-boot-gyro-not-started.log'));
  assert.equal(result.passed, false);
  assert.match(result.failures[0], /^first sample: the gyro reads exactly zero at t=332690 us/);
});

test('samples printed before the reset do not count as the first one after boot', () => {
  const parsed = [parseLine('[imu] 900000 0.010 0.020 1.000 0.0 0.0 0.0'), ...fixture('cardputer-boot.log')];
  assert.deepEqual(checkFirstSampleSettled(parsed).failures, []);
});

test('a boot that never streams a sample fails the first-sample check', () => {
  const parsed = ['[boot] power OK: x', '[boot] Cardputer ADV ready'].map(parseLine);
  assert.deepEqual(checkFirstSampleSettled(parsed).failures, ['first sample: none arrived after boot']);
});

test('a FAILED boot line fails the boot and names the subsystem and reason', () => {
  const parsed = ['[boot] power OK: x', '[boot] display OK: y', '[boot] input OK: z', '[boot] imu FAILED: no ACK at 0x68'].map(parseLine);
  const result = checkBoot(parsed);
  assert.equal(result.passed, false);
  assert.ok(result.failures.includes('boot: imu FAILED: no ACK at 0x68'));
  assert.ok(result.failures.includes('boot: no "[boot] imu OK" line'));
  assert.ok(result.failures.includes('boot: never reported ready'));
});

test('an IMU configured under the required ranges fails the boot', () => {
  const parsed = [
    '[boot] power OK: x', '[boot] display OK: y', '[boot] input OK: z',
    '[boot] imu OK: MPU6886 0x68 accel +-2 g gyro +-250 dps', '[boot] M5StickC Plus2 ready',
  ].map(parseLine);
  assert.deepEqual(checkBoot(parsed).failures, [
    'boot: accel range +-2 g is under +-16 g',
    'boot: gyro range +-250 dps is under +-2000 dps',
  ]);
});

test('the skeleton\'s old +-8 g fails the boot: shakes reach 9.8 g (ADR-0005)', () => {
  const parsed = [
    '[boot] power OK: x', '[boot] display OK: y', '[boot] input OK: z',
    '[boot] imu OK: BMI270 0x69 accel +-8 g gyro +-2000 dps at 100 Hz', '[boot] Cardputer ADV ready',
  ].map(parseLine);
  assert.deepEqual(checkBoot(parsed).failures, ['boot: accel range +-8 g is under +-16 g']);
});

test('a failed IMU read, an IDF error and a panic are each an error line', () => {
  const parsed = [
    '[imu] read FAILED (1 failed so far)',
    '[   305][E][Wire.cpp:520] requestFrom(): i2cWriteReadNonStop returned Error 259',
    'E (178) esp_core_dump_flash: Core dump flash config is corrupted!',
    'Guru Meditation Error: Core  0 panic\'ed (LoadProhibited). Exception was unhandled.',
    '[   294][I][esp32-hal-i2c-ng.c:112] i2cInit(): Initializing I2C Master',
  ].map(parseLine);
  assert.equal(checkNoErrors(parsed).failures.length, 4);
});

test('a sample line parses into its timestamp and six values', () => {
  assert.deepEqual(parseLine('[imu] 608216 0.010 -0.264 0.959 -0.3 -0.5 120.1'), {
    kind: 'sample',
    sample: { t: 608216, ax: 0.01, ay: -0.264, az: 0.959, gx: -0.3, gy: -0.5, gz: 120.1 },
  });
});

test('a flat, still board passes rest and reports its noise and sample rate', () => {
  const result = checkRest(still(0.01, 0.02, 0.99));
  assert.deepEqual(result.failures, []);
  assert.ok(Math.abs(result.measured.sampleRateHz - 50) < 1e-9);
  assert.ok(result.measured.noiseStdDev.az < 1e-12);
});

test('rest fails when |a| is out of tolerance', () => {
  const result = checkRest(still(0, 0, 1.2));
  assert.deepEqual(result.failures, ['rest: |a| is 1.200 g, more than 0.15 g from 1 g']);
});

test('rest fails when the wrong axis is nearest +1 g, as a wrong axis map would make it', () => {
  const result = checkRest(still(0.02, 0.99, 0.01));
  assert.deepEqual(result.failures, ['rest: y, not z, is nearest +1 g']);
});

test('rest fails when a gyro axis is not near zero', () => {
  const samples = still(0, 0, 1);
  samples[10].gx = -12;
  assert.deepEqual(checkRest(samples).failures, ['rest: gx reached 12 dps, over 10']);
});

test('an orientation passes only on its own axis', () => {
  assert.equal(checkOrientation(still(0.02, 0.98, 0.05), 'y', 'top edge up').passed, true);
  assert.deepEqual(checkOrientation(still(-0.98, 0.02, 0.05), 'x', 'right edge up').failures, [
    'right edge up: z, not x, is nearest +1 g',
  ]);
});

test('a counterclockwise twist must peak above +90 dps; a clockwise one fails', () => {
  const twist = (peak) => series(50, (i) => ({ ax: 0, ay: 0, az: 1, gx: 0, gy: 0, gz: i === 25 ? peak : 0 }));
  assert.equal(checkTwist(twist(150)).passed, true);
  assert.deepEqual(checkTwist(twist(-150)).failures, ['twist: gz peaked at 0 dps, not above +90']);
});

test('a shake must peak above 2 g', () => {
  const shake = (peak) => series(50, (i) => ({ ax: i === 10 ? peak : 0, ay: 0, az: 1, gx: 0, gy: 0, gz: 0 }));
  assert.equal(checkShake(shake(3.5)).passed, true);
  assert.deepEqual(checkShake(shake(1.6)).failures, ['shake: |a| peaked at 1.89 g, not above 2 g']);
});

test('a window with no samples fails rather than passing vacuously', () => {
  assert.deepEqual(checkRest([]).failures, ['rest: no IMU samples arrived']);
  assert.deepEqual(checkShake([]).failures, ['shake: no IMU samples arrived']);
});

test('presses need exactly one PRIMARY and one SECONDARY', () => {
  assert.equal(checkPresses(['PRIMARY', 'SECONDARY']).passed, true);
  assert.deepEqual(checkPresses(['PRIMARY', 'PRIMARY']).failures, [
    'presses: 2 "[input] PRIMARY" lines, expected 1',
    'presses: 0 "[input] SECONDARY" lines, expected 1',
  ]);
  assert.deepEqual(checkPresses(['PRIMARY', 'SECONDARY', 'LONG_PRIMARY']).failures, [
    'presses: unexpected actions LONG_PRIMARY',
  ]);
});

test('arguments need a skeleton env and a port', () => {
  assert.deepEqual(parseArgs(['--env', 'cardputer-skeleton', '--port', '/dev/ttyACM0']), {
    env: 'cardputer-skeleton',
    port: '/dev/ttyACM0',
  });
  assert.throws(() => parseArgs(['--env', 'custom', '--port', 'p']), /--env must be one of/);
  // The product no longer prints the readout this script checks (slice 0011).
  assert.throws(() => parseArgs(['--env', 'cardputer', '--port', 'p']), /--env must be one of/);
  assert.throws(() => parseArgs(['--env', 'cardputer-skeleton']), /--port is required/);
});
