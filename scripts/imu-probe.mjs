#!/usr/bin/env node
// IMU probe (slice 0004): which accelerometer range, output rate and filtering tell a
// deliberate shake from the device being handled.
//
// Walks the operator through the gesture protocol on a board running the probe firmware,
// recording each take with scripts/capture_imu.py, then analyses every take on disk:
// per take, the peak |a|, saturated samples and peak |w|; per candidate rate and detector
// feature, the worst shake, the worst handling gesture and the margin between them.
// Writes artifacts/imu-probe/<env>/: one CSV per take, summary.json and summary.md.
//
// Usage: node scripts/imu-probe.mjs --env cardputer-imu-probe --port /dev/ttyACM0
//          [--gestures shake-hard,bump]   record only these (again), keeping the other takes
//          [--analyse-only]               record nothing, analyse the takes already on disk
// Flash the probe first: pio run -e <env> -t upload
//
// Everything above main() is pure and tested by scripts/imu-probe.test.mjs.

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import { pathToFileURL } from 'node:url';

export const ENVS = ['cardputer-imu-probe', 'm5stickc-plus2-imu-probe', 'cardputer-imu-probe-100hz', 'm5stickc-plus2-imu-probe-100hz'];
export const TAKES_PER_GESTURE = 3;

export const GESTURES = [
  { name: 'rest', kind: 'handling', instruction: 'Lay the board flat on the desk and leave it there.' },
  { name: 'hold', kind: 'handling', instruction: 'Hold it still in front of you, screen toward your face, as you would to read it.' },
  { name: 'pickup', kind: 'handling', instruction: 'Start with it flat on the desk; after Enter, pick it up into the reading pose.' },
  { name: 'setdown', kind: 'handling', instruction: 'Start in the reading pose; after Enter, put it down flat on the desk.' },
  { name: 'walk', kind: 'handling', instruction: 'Hold it in the reading pose; after Enter, walk a few steps with it.' },
  { name: 'tilt', kind: 'handling', instruction: 'Hold it in the reading pose; after Enter, tilt it slowly toward each edge in turn, as if sliding dice.' },
  { name: 'bump', kind: 'handling', instruction: 'Hold it; after Enter, knock it once against the desk edge, as if by accident.' },
  { name: 'shake-gentle', kind: 'shake', instruction: 'After Enter, give it the softest shake you would still mean as a roll.' },
  { name: 'shake-hard', kind: 'shake', instruction: 'After Enter, shake it hard, like a dice cup.' },
];

// Rates a lower product setting could run at. A candidate is simulated only when it
// divides the capture rate, which keeps it a rate the same chip offers: the BMI270's are
// 25 x 2^n Hz, the MPU6886's 1 kHz / (1 + divider).
export const CANDIDATE_RATES_HZ = [500, 400, 250, 200, 100];
// About one stroke of a shake (3 to 5 strokes a second), so the RMS rises with sustained
// shaking but a single knock is diluted by the quiet around it.
export const RMS_WINDOW_MS = 200;
// A knock is one large impulse and a shake a sustained one, so the feature that tells them
// apart measures how long the motion lasts rather than how large it is: the share of the
// busiest second spent more than 0.5 g off 1 g. Picked on 2026-10-23 from a sweep over
// the StickC's takes (0.3 or 0.5 g, 0.5 or 1 s), where every size feature failed because
// a knock out-peaked the gentle shakes; fixed before the Cardputer's takes, which test it
// (slice 0004).
export const BUSY_DEVIATION_G = 0.5;
export const BUSY_WINDOW_MS = 1000;
export const GRAVITY_G = 1;
const MS_PER_S = 1000;
const US_PER_S = 1e6;
// The firmware's micros() wraps every 71 minutes, so a take can straddle the wrap.
const TIMESTAMP_MODULUS = 2 ** 32;

export const FEATURES = {
  peakDeviationG: 'peak of ||a| - 1 g|',
  rmsDeviationG: `largest ${RMS_WINDOW_MS} ms RMS of |a| - 1 g`,
  peakGyroDps: 'peak |w|',
  busyShare: `share of the busiest ${BUSY_WINDOW_MS} ms over ${BUSY_DEVIATION_G} g off 1 g`,
};

const AXES = ['ax', 'ay', 'az', 'gx', 'gy', 'gz'];
const CSV_COLUMNS = ['t_us', ...AXES, 'accel_saturated', 'gyro_saturated'];
const TAKE_FILE = /^([a-z-]+)-(\d+)\.csv$/;

/** One take as capture_imu.py writes it. */
export function parseCaptureCsv(text) {
  // Python's csv module ends rows with \r\n, as RFC 4180 has it.
  const [header, ...lines] = text.trim().split(/\r?\n/);
  if (header !== CSV_COLUMNS.join(',')) throw new Error(`unexpected CSV header "${header}"`);
  return lines.map((line) => {
    const values = line.split(',').map(Number);
    if (values.length !== CSV_COLUMNS.length || values.some(Number.isNaN)) throw new Error(`malformed CSV row "${line}"`);
    const [t, ax, ay, az, gx, gy, gz, accelSaturated, gyroSaturated] = values;
    return { t, ax, ay, az, gx, gy, gz, accelSaturated: accelSaturated === 1, gyroSaturated: gyroSaturated === 1 };
  });
}

export const accelMagnitude = (s) => Math.hypot(s.ax, s.ay, s.az);
export const gyroMagnitude = (s) => Math.hypot(s.gx, s.gy, s.gz);

/**
 * Averages each run of `factor` samples into one, as a chip running `factor` times slower
 * roughly does; a trailing partial block is dropped rather than averaged short.
 */
export function decimate(samples, factor) {
  if (!Number.isInteger(factor) || factor < 1) throw new Error(`decimation factor ${factor} is not a positive integer`);
  const blocks = [];
  for (let start = 0; start + factor <= samples.length; start += factor) {
    const block = samples.slice(start, start + factor);
    const mean = (key) => block.reduce((sum, s) => sum + s[key], 0) / factor;
    blocks.push({
      t: block[0].t,
      ...Object.fromEntries(AXES.map((axis) => [axis, mean(axis)])),
      accelSaturated: block.some((s) => s.accelSaturated),
      gyroSaturated: block.some((s) => s.gyroSaturated),
    });
  }
  return blocks;
}

/** The candidate rates for a capture: the capture rate itself, then each that divides it. */
export function candidateRates(captureRateHz) {
  return [captureRateHz, ...CANDIDATE_RATES_HZ.filter((rate) => rate < captureRateHz && captureRateHz % rate === 0)];
}

/** The detector features of one take at one rate; each is the take's worst moment. */
export function features(samples, rateHz) {
  if (samples.length === 0) throw new Error('a take with no samples has no features');
  const deviations = samples.map((s) => accelMagnitude(s) - GRAVITY_G);
  const windowLength = Math.min(samples.length, Math.max(1, Math.round((rateHz * RMS_WINDOW_MS) / MS_PER_S)));
  let sumOfSquares = 0;
  let largestMeanSquare = 0;
  deviations.forEach((deviation, i) => {
    sumOfSquares += deviation * deviation;
    if (i >= windowLength) sumOfSquares -= deviations[i - windowLength] ** 2;
    if (i >= windowLength - 1) largestMeanSquare = Math.max(largestMeanSquare, sumOfSquares / windowLength);
  });
  return {
    peakDeviationG: Math.max(...deviations.map(Math.abs)),
    // A running sum can drift a hair below zero after subtracting what it added.
    rmsDeviationG: Math.sqrt(Math.max(0, largestMeanSquare)),
    peakGyroDps: Math.max(...samples.map(gyroMagnitude)),
    busyShare: busyShare(deviations, rateHz),
  };
}

/** The largest share of any whole window whose samples deviate over BUSY_DEVIATION_G. */
function busyShare(deviations, rateHz) {
  const windowLength = Math.min(deviations.length, Math.round((rateHz * BUSY_WINDOW_MS) / MS_PER_S));
  const busy = deviations.map((deviation) => (Math.abs(deviation) > BUSY_DEVIATION_G ? 1 : 0));
  let inWindow = 0;
  let largest = 0;
  busy.forEach((flag, i) => {
    inWindow += flag;
    if (i >= windowLength) inWindow -= busy[i - windowLength];
    if (i >= windowLength - 1) largest = Math.max(largest, inWindow);
  });
  return largest / windowLength;
}

/** What a take shows at the capture rate, before any decimation. */
export function describeTake(samples) {
  if (samples.length < 2) throw new Error(`a take needs at least 2 samples to time, got ${samples.length}`);
  const spanUs = (((samples.at(-1).t - samples[0].t) % TIMESTAMP_MODULUS) + TIMESTAMP_MODULUS) % TIMESTAMP_MODULUS;
  return {
    samples: samples.length,
    // The chip's own clock, which the configured rate only names.
    measuredRateHz: ((samples.length - 1) * US_PER_S) / spanUs,
    peakAccelG: Math.max(...samples.map(accelMagnitude)),
    accelSaturatedSamples: samples.filter((s) => s.accelSaturated).length,
    peakGyroDps: Math.max(...samples.map(gyroMagnitude)),
    gyroSaturatedSamples: samples.filter((s) => s.gyroSaturated).length,
  };
}

/**
 * For each candidate rate and feature: the smallest value any shake take reached, the
 * largest any handling take reached, and the margin between them. A positive margin means
 * a threshold between the two separates every take recorded; the midpoint is offered as
 * that threshold. A missing kind leaves nothing to compare, so it fails loud.
 * @param takes [{gesture, kind, take, samples}]
 */
export function separation(takes, captureRateHz) {
  for (const kind of ['shake', 'handling']) {
    if (!takes.some((t) => t.kind === kind)) throw new Error(`no ${kind} takes to compare`);
  }
  return candidateRates(captureRateHz).map((rateHz) => {
    const factor = captureRateHz / rateHz;
    const measured = takes.map((t) => ({ ...t, features: features(decimate(t.samples, factor), rateHz) }));
    const byFeature = Object.fromEntries(Object.keys(FEATURES).map((feature) => {
      const extreme = (kind, pick) => measured.filter((t) => t.kind === kind)
        .reduce((best, t) => (best === null || pick(t.features[feature], best.value) ? { value: t.features[feature], take: `${t.gesture}-${t.take}` } : best), null);
      const worstShake = extreme('shake', (a, b) => a < b);
      const worstHandling = extreme('handling', (a, b) => a > b);
      const margin = worstShake.value - worstHandling.value;
      return [feature, { worstShake, worstHandling, margin, threshold: margin > 0 ? (worstShake.value + worstHandling.value) / 2 : null }];
    }));
    return { rateHz, features: byFeature };
  });
}

export function parseArgs(argv) {
  const args = { env: null, port: null, gestures: null, analyseOnly: false };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--env') args.env = argv[++i] ?? null;
    else if (argv[i] === '--port') args.port = argv[++i] ?? null;
    else if (argv[i] === '--gestures') args.gestures = (argv[++i] ?? '').split(',').filter(Boolean);
    else if (argv[i] === '--analyse-only') args.analyseOnly = true;
    else throw new Error(`unknown argument ${argv[i]}`);
  }
  if (!ENVS.includes(args.env)) throw new Error(`--env must be one of ${ENVS.join(', ')}`);
  if (!args.analyseOnly && !args.port) throw new Error('--port is required, e.g. --port /dev/ttyACM0');
  const unknown = (args.gestures ?? []).filter((name) => !GESTURES.some((g) => g.name === name));
  if (unknown.length > 0) throw new Error(`unknown gesture(s) ${unknown.join(', ')}; the protocol has ${GESTURES.map((g) => g.name).join(', ')}`);
  return args;
}

// ---------------------------------------------------------------- recording and reporting

const takePath = (outDir, gesture, take) => join(outDir, `${gesture}-${take}.csv`);
// capture_imu.py's summary of a take, kept beside its CSV for the rate the board declared.
const capturePath = (csvPath) => csvPath.replace(/\.csv$/, '.json');

async function record(port, outDir, gestures) {
  const prompt = createInterface({ input: process.stdin, output: process.stdout });
  const ask = (question) => new Promise((resolve) => prompt.question(`\n${question}\n  press Enter on this computer when ready `, resolve));
  // readline swallows Ctrl+C in raw mode, which would leave the run hanging; abort instead.
  prompt.on('SIGINT', () => {
    console.error('\naborted by the operator; the takes recorded so far are kept');
    process.exit(1);
  });
  try {
    for (const gesture of GESTURES) {
      for (let take = 1; take <= TAKES_PER_GESTURE; take++) {
        const path = takePath(outDir, gesture.name, take);
        // Named gestures are recorded again; otherwise a rerun picks up where an
        // interrupted one stopped.
        const wanted = gestures === null ? !existsSync(path) : gestures.includes(gesture.name);
        if (!wanted) continue;
        await ask(`${gesture.name}, take ${take} of ${TAKES_PER_GESTURE}: ${gesture.instruction}`);
        console.log('  recording now, then sending...');
        const capture = spawnSync('python3', ['scripts/capture_imu.py', '--port', port, '--out', path], { encoding: 'utf8' });
        if (capture.status !== 0) throw new Error(capture.stderr.trim() || `capture_imu.py exited with ${capture.status}`);
        writeFileSync(capturePath(path), capture.stdout);
        console.log(`  saved ${path}`);
      }
    }
  } finally {
    prompt.close();
  }
}

function loadTakes(outDir) {
  return readdirSync(outDir).flatMap((file) => {
    const match = TAKE_FILE.exec(file);
    const gesture = match && GESTURES.find((g) => g.name === match[1]);
    if (!gesture) return [];
    const path = join(outDir, file);
    const { rateHz } = JSON.parse(readFileSync(capturePath(path), 'utf8'));
    return [{ gesture: gesture.name, kind: gesture.kind, take: Number(match[2]), rateHz, samples: parseCaptureCsv(readFileSync(path, 'utf8')) }];
  });
}

/** The rate the board declared for every take: a mixed set cannot be decimated alike. */
function captureRateOf(takes) {
  const rates = new Set(takes.map((t) => t.rateHz));
  if (rates.size !== 1) throw new Error(`takes were recorded at different rates (${[...rates].join(', ')} Hz); keep one set per directory`);
  return [...rates][0];
}

const fixed = (value, digits) => (value === null ? '-' : value.toFixed(digits));

function markdownSummary(env, captureRateHz, perTake, rates) {
  const lines = [`# IMU probe: ${env}`, '', `Capture rate ${captureRateHz} Hz.`, '', '## Takes', '',
    '| Take | Samples | Measured rate (Hz) | Peak \\|a\\| (g) | Accel saturated | Peak \\|w\\| (dps) | Gyro saturated |', '|---|---|---|---|---|---|---|'];
  for (const t of perTake) {
    lines.push(`| ${t.take} | ${t.samples} | ${fixed(t.measuredRateHz, 1)} | ${fixed(t.peakAccelG, 2)} | ${t.accelSaturatedSamples} | ${fixed(t.peakGyroDps, 0)} | ${t.gyroSaturatedSamples} |`);
  }
  lines.push('', '## Separation', '', '| Rate (Hz) | Feature | Worst shake | Worst handling | Margin | Threshold |', '|---|---|---|---|---|---|');
  for (const { rateHz, features: byFeature } of rates) {
    for (const [feature, s] of Object.entries(byFeature)) {
      lines.push(`| ${rateHz} | ${FEATURES[feature]} | ${fixed(s.worstShake.value, 3)} (${s.worstShake.take}) | ${fixed(s.worstHandling.value, 3)} (${s.worstHandling.take}) | ${fixed(s.margin, 3)} | ${fixed(s.threshold, 3)} |`);
    }
  }
  return `${lines.join('\n')}\n`;
}

async function main() {
  const { env, port, gestures, analyseOnly } = parseArgs(process.argv.slice(2));
  const outDir = join('artifacts', 'imu-probe', env);
  mkdirSync(outDir, { recursive: true });
  if (!analyseOnly) await record(port, outDir, gestures);

  const takes = loadTakes(outDir).sort((a, b) => a.gesture.localeCompare(b.gesture) || a.take - b.take);
  const captureRateHz = captureRateOf(takes);
  const perTake = takes.map((t) => ({ take: `${t.gesture}-${t.take}`, ...describeTake(t.samples) }));
  const rates = separation(takes, captureRateHz);
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ env, captureRateHz, takes: perTake, separation: rates }, null, 2)}\n`);
  const markdown = markdownSummary(env, captureRateHz, perTake, rates);
  writeFileSync(join(outDir, 'summary.md'), markdown);
  console.log(`\n${markdown}\nArtifacts in ${outDir}`);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
