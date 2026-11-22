// Tests for the pure half of scripts/imu-probe.mjs (slice 0004), on synthetic series
// whose answers can be worked out by hand.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  GESTURES,
  OPERATOR_PROTOCOL,
  PARTICIPANT_PROTOCOL,
  candidateRates,
  checkFixtureRate,
  decimate,
  detectedShakes,
  describeTake,
  features,
  fixtureCsv,
  fixtureName,
  fixturePrefix,
  parseArgs,
  parseCaptureCsv,
  separation,
  takeRows,
} from './imu-probe.mjs';

const RATE_HZ = 1000;
const PERIOD_US = 1000;
const CLOSE = 1e-9;

/** `count` samples 1 ms apart, at rest with gravity on z, each changed by `change(index)`. */
function series(count, change = () => ({})) {
  return Array.from({ length: count }, (_, i) => ({
    t: i * PERIOD_US, ax: 0, ay: 0, az: 1, gx: 0, gy: 0, gz: 0, accelSaturated: false, gyroSaturated: false, ...change(i),
  }));
}

test('a capture CSV parses into samples with saturation flags', () => {
  const csv = 't_us,ax,ay,az,gx,gy,gz,accel_saturated,gyro_saturated\n10,0.5,-1,1,100,0,-3.5,1,0\n';
  assert.deepEqual(parseCaptureCsv(csv), [
    { t: 10, ax: 0.5, ay: -1, az: 1, gx: 100, gy: 0, gz: -3.5, accelSaturated: true, gyroSaturated: false },
  ]);
  assert.throws(() => parseCaptureCsv('t,ax\n1,2\n'), /unexpected CSV header/);
});

test('a capture CSV with the CRLF line ends Python\'s csv module writes parses the same', () => {
  const csv = 't_us,ax,ay,az,gx,gy,gz,accel_saturated,gyro_saturated\r\n10,0.5,-1,1,100,0,-3.5,0,1\r\n';
  assert.deepEqual(parseCaptureCsv(csv), [
    { t: 10, ax: 0.5, ay: -1, az: 1, gx: 100, gy: 0, gz: -3.5, accelSaturated: false, gyroSaturated: true },
  ]);
  assert.throws(() => parseCaptureCsv('t_us,ax,ay,az,gx,gy,gz,accel_saturated,gyro_saturated\n1,x,0,0,0,0,0,0,0\n'), /malformed/);
});

test('decimation averages whole blocks and drops a partial one', () => {
  const samples = series(5, (i) => ({ ax: i, gyroSaturated: i === 1 }));
  const blocks = decimate(samples, 2);
  assert.deepEqual(blocks.map((b) => b.ax), [0.5, 2.5]);
  assert.deepEqual(blocks.map((b) => b.t), [0, 2000]);
  assert.deepEqual(blocks.map((b) => b.gyroSaturated), [true, false]);
  assert.throws(() => decimate(samples, 2.5), /not a positive integer/);
});

test('candidate rates are the capture rate and those that divide it', () => {
  assert.deepEqual(candidateRates(800), [800, 400, 200, 100]);
  assert.deepEqual(candidateRates(1000), [1000, 500, 250, 200, 100]);
});

test('a board at rest has no deviation from 1 g', () => {
  const result = features(series(400), RATE_HZ);
  assert.ok(result.peakDeviationG < CLOSE);
  assert.ok(result.rmsDeviationG < CLOSE);
  assert.equal(result.peakGyroDps, 0);
});

test('one 3 g spike peaks at 2 g but is diluted across the RMS window', () => {
  const result = features(series(400, (i) => (i === 100 ? { az: 3 } : {})), RATE_HZ);
  assert.ok(Math.abs(result.peakDeviationG - 2) < CLOSE);
  // 200 ms at 1 kHz is 200 samples, one of which deviates by 2 g.
  assert.ok(Math.abs(result.rmsDeviationG - Math.sqrt(4 / 200)) < CLOSE);
});

test('sustained shaking keeps its RMS where a single spike does not', () => {
  // |a| alternates 3 g and 1 g (az = -1 is still 1 g), deviating 2 and 0: RMS sqrt(2).
  const shaking = features(series(400, (i) => ({ az: i % 2 === 0 ? 3 : -1 })), RATE_HZ);
  assert.ok(Math.abs(shaking.rmsDeviationG - Math.SQRT2) < CLOSE);
});

test('decimating a one-sample spike lowers its peak by the block size', () => {
  const spike = series(800, (i) => (i === 100 ? { az: 9 } : {}));
  assert.ok(Math.abs(features(decimate(spike, 8), 125).peakDeviationG - 1) < CLOSE);
});

test('a huge knock is a small busy share, steady modest shaking a large one', () => {
  // 30 ms at 18 g in 2 s: 30 of the busiest second's 1000 samples are over 0.5 g.
  const knock = features(series(2000, (i) => (i >= 500 && i < 530 ? { az: 18 } : {})), RATE_HZ);
  assert.ok(Math.abs(knock.busyShare - 0.03) < CLOSE);
  // |a| alternates 2 g and 1 g: half of every second is 1 g over, the other half on 1 g.
  const shaking = features(series(2000, (i) => ({ az: i % 2 === 0 ? 2 : 1 })), RATE_HZ);
  assert.ok(Math.abs(shaking.busyShare - 0.5) < CLOSE);
});

test('the busy share counts only deviations over the threshold, on either side of 1 g', () => {
  const justUnder = features(series(1000, () => ({ az: 1.5 })), RATE_HZ);
  assert.equal(justUnder.busyShare, 0);
  const freeFall = features(series(1000, () => ({ az: 0.4 })), RATE_HZ);
  assert.equal(freeFall.busyShare, 1);
});

test('the busy share is the busiest whole window, not one cut short by the take', () => {
  // 600 busy samples at the end of a 1500-sample take: the last full second holds all 600.
  const late = features(series(1500, (i) => (i >= 900 ? { az: 3 } : {})), RATE_HZ);
  assert.ok(Math.abs(late.busyShare - 0.6) < CLOSE);
});

test('the gyro feature is the peak rotation rate on any axes', () => {
  const result = features(series(10, (i) => (i === 3 ? { gx: 300, gy: 400 } : {})), RATE_HZ);
  assert.equal(result.peakGyroDps, 500);
});

test('a take reports its peak, saturation and measured rate, even across a timestamp wrap', () => {
  const top = 2 ** 32;
  const samples = series(5, (i) => ({ t: (top - 2000 + i * 1000) % top, az: i === 2 ? 16 : 1, accelSaturated: i === 2 }));
  const take = describeTake(samples);
  assert.equal(take.peakAccelG, 16);
  assert.equal(take.accelSaturatedSamples, 1);
  assert.equal(take.measuredRateHz, 1000);
});

test('a take too short to time is refused rather than given an infinite rate', () => {
  assert.throws(() => describeTake(series(1)), /at least 2 samples/);
});

test('separation names the worst take of each kind and a threshold between them', () => {
  const takes = [
    { gesture: 'shake-hard', kind: 'shake', take: 1, samples: series(400, (i) => ({ az: i % 2 === 0 ? 5 : -3 })) },
    { gesture: 'shake-gentle', kind: 'shake', take: 1, samples: series(400, (i) => ({ az: i % 2 === 0 ? 3 : -1 })) },
    { gesture: 'walk', kind: 'handling', take: 2, samples: series(400, (i) => ({ az: i % 2 === 0 ? 1.5 : 0.5 })) },
    { gesture: 'rest', kind: 'handling', take: 1, samples: series(400) },
  ];
  const [atCapture] = separation(takes, 1000);
  const rms = atCapture.features.rmsDeviationG;
  assert.equal(atCapture.rateHz, 1000);
  assert.equal(rms.worstShake.take, 'shake-gentle-1');
  assert.equal(rms.worstHandling.take, 'walk-2');
  // The gentle shake's RMS is sqrt(2) (deviations 2 and 0); the walk's is 0.5.
  assert.ok(Math.abs(rms.margin - (Math.SQRT2 - 0.5)) < CLOSE);
  assert.ok(Math.abs(rms.threshold - (Math.SQRT2 + 0.5) / 2) < CLOSE);
});

test('overlapping classes get no threshold', () => {
  const takes = [
    { gesture: 'shake-gentle', kind: 'shake', take: 1, samples: series(400, (i) => (i === 10 ? { az: 2 } : {})) },
    { gesture: 'bump', kind: 'handling', take: 1, samples: series(400, (i) => (i === 10 ? { az: 4 } : {})) },
  ];
  const peak = separation(takes, 1000)[0].features.peakDeviationG;
  assert.ok(peak.margin < 0);
  assert.equal(peak.threshold, null);
});

test('separation needs both kinds of take', () => {
  const shakeOnly = [{ gesture: 'shake-hard', kind: 'shake', take: 1, samples: series(10) }];
  assert.throws(() => separation(shakeOnly, 1000), /no handling takes/);
});

test('the protocol has both kinds, and every gesture says what to do', () => {
  assert.deepEqual([...new Set(GESTURES.map((g) => g.kind))].sort(), ['handling', 'shake']);
  assert.ok(GESTURES.every((g) => g.instruction.length > 0));
});

test('arguments need a probe env, a port unless only analysing, and known gestures', () => {
  assert.deepEqual(parseArgs(['--env', 'cardputer-imu-probe', '--port', 'p']), {
    env: 'cardputer-imu-probe', port: 'p', gestures: null, participant: null, analyseOnly: false, exportFixtures: false,
  });
  assert.equal(parseArgs(['--env', 'm5stickc-plus2-imu-probe', '--analyse-only']).port, null);
  assert.deepEqual(parseArgs(['--env', 'cardputer-imu-probe', '--port', 'p', '--gestures', 'bump,walk']).gestures, ['bump', 'walk']);
  assert.throws(() => parseArgs(['--env', 'cardputer', '--port', 'p']), /--env must be one of/);
  assert.throws(() => parseArgs(['--env', 'cardputer-imu-probe']), /--port is required/);
  assert.throws(() => parseArgs(['--env', 'cardputer-imu-probe', '--port', 'p', '--gestures', 'juggle']), /unknown gesture/);
});

test('a participant is an anonymous p-number, and exporting needs one', () => {
  const args = parseArgs(['--env', 'cardputer-imu-probe-100hz', '--port', 'p', '--participant', 'p12']);
  assert.equal(args.participant, 'p12');
  assert.equal(args.exportFixtures, false);
  assert.equal(parseArgs(['--env', 'cardputer-imu-probe-100hz', '--port', 'p']).participant, null);
  assert.equal(parseArgs(['--env', 'cardputer-imu-probe-100hz', '--analyse-only', '--participant', 'p2', '--export-fixtures']).exportFixtures, true);
  for (const name of ['alice', 'P2', 'p', 'p2a', '../p2', '']) {
    assert.throws(() => parseArgs(['--env', 'cardputer-imu-probe-100hz', '--port', 'p', '--participant', name]), /never a name/, name);
  }
  assert.throws(() => parseArgs(['--env', 'cardputer-imu-probe-100hz', '--analyse-only', '--export-fixtures']), /needs --participant/);
});

test('the operator keeps the original protocol, and a participant gets the near-threshold handling and every shake', () => {
  const names = GESTURES.map((g) => g.name);
  assert.ok([...OPERATOR_PROTOCOL, ...PARTICIPANT_PROTOCOL].every((name) => names.includes(name)));
  assert.deepEqual(OPERATOR_PROTOCOL, ['rest', 'hold', 'pickup', 'setdown', 'walk', 'tilt', 'bump', 'shake-gentle', 'shake-hard']);
  const kindOf = (name) => GESTURES.find((g) => g.name === name).kind;
  assert.deepEqual(PARTICIPANT_PROTOCOL.filter((name) => kindOf(name) === 'handling'), ['pickup', 'setdown', 'walk', 'bump']);
  assert.deepEqual(PARTICIPANT_PROTOCOL.filter((name) => kindOf(name) === 'shake'), names.filter((name) => kindOf(name) === 'shake'));
});

test('a take is labelled with its participant, and the operator\'s without one', () => {
  const quiet = series(400);
  const busy = series(400, (i) => ({ az: i % 2 === 0 ? 3 : -3 }));
  const takes = [
    { participant: null, gesture: 'walk', kind: 'handling', take: 1, samples: quiet },
    { participant: 'p2', gesture: 'shake-slow', kind: 'shake', take: 3, samples: busy },
  ];
  const [atCapture] = separation(takes, RATE_HZ);
  assert.equal(atCapture.features.busyShare.worstShake.take, 'p2/shake-slow-3');
  assert.equal(atCapture.features.busyShare.worstHandling.take, 'walk-1');
  const rows = takeRows(takes, RATE_HZ);
  assert.deepEqual(rows.map((row) => row.take), ['walk-1', 'p2/shake-slow-3']);
  // Every sample of the shaking take reads 3 g, so 2 g off 1 g; none of the quiet one is off.
  assert.deepEqual(rows.map((row) => row.busyShare), [0, 1]);
});

test('a fixture is the capture cut to t_us,ax,ay,az with the values\' text kept and LF line ends', () => {
  const capture = 't_us,ax,ay,az,gx,gy,gz,accel_saturated,gyro_saturated\r\n'
    + '1025614146,-0.05517578125,0.3681640625,0.908203125,1.5,-2,0,0,0\r\n'
    + '1025624189,1e-3,0.35791015625,16,0,0,0,1,0\r\n';
  assert.equal(fixtureCsv(capture),
    't_us,ax,ay,az\n1025614146,-0.05517578125,0.3681640625,0.908203125\n1025624189,1e-3,0.35791015625,16\n');
  assert.throws(() => fixtureCsv('t_us,ax,ay,az\n1,0,0,1\n'), /unexpected CSV header/);
});

test('a fixture is named by board and participant, from either probe env', () => {
  assert.equal(fixtureName('cardputer-imu-probe-100hz', 'p2', 'shake-one-hand', 3), 'cardputer-p2-shake-one-hand-3.csv');
  assert.equal(fixtureName('m5stickc-plus2-imu-probe', 'p4', 'shake-slow', 1), 'm5stickc-plus2-p4-shake-slow-1.csv');
  assert.throws(() => fixtureName('cardputer', 'p2', 'shake-slow', 1), /not an IMU probe env/);
  // The prefix a re-export clears names one participant on one board, and no one else.
  const prefix = fixturePrefix('cardputer-imu-probe-100hz', 'p2');
  assert.equal(prefix, 'cardputer-p2-');
  assert.ok(!'cardputer-p21-shake-slow-1.csv'.startsWith(prefix));
  assert.ok(!'m5stickc-plus2-p2-shake-slow-1.csv'.startsWith(prefix));
  assert.ok(!'cardputer-shake-gentle-1.csv'.startsWith(prefix));
});

test('only a take at the device\'s 100 Hz becomes a fixture', () => {
  assert.doesNotThrow(() => checkFixtureRate(100, 'p2/shake-slow-1'));
  assert.throws(() => checkFixtureRate(800, 'p2/shake-slow-1'), /p2\/shake-slow-1 was recorded at 800 Hz/);
});

test('only the shakes the device would roll on are exported, and the operator\'s takes never are', () => {
  // At 100 Hz a second is 100 samples, so 28 and 27 busy ones fall either side of the product's 0.275.
  const PRODUCT_RATE_HZ = 100;
  const busyFor = (count) => series(2 * PRODUCT_RATE_HZ, (i) => ({ az: i < count ? 3 : 1 }));
  const takes = [
    { participant: 'p2', gesture: 'shake-slow', kind: 'shake', take: 1, samples: busyFor(28) },
    { participant: 'p2', gesture: 'shake-slow', kind: 'shake', take: 2, samples: busyFor(27) },
    { participant: 'p2', gesture: 'walk', kind: 'handling', take: 1, samples: busyFor(100) },
    { participant: null, gesture: 'shake-hard', kind: 'shake', take: 1, samples: busyFor(100) },
  ];
  const { detected, missed } = detectedShakes(takes, 'p2', PRODUCT_RATE_HZ);
  assert.deepEqual(detected.map((t) => t.take), [1]);
  assert.deepEqual(missed.map((t) => t.take), [2]);
});
