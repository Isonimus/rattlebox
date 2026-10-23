#!/usr/bin/env node
// Fairness probe (slice 0008): measures what the rest thresholds, hold time and cocked tolerance
// are chosen from, and tallies rolls of each die type through the real physics for the
// chi-square test of invariant #2.
//
// Builds the native-fairness-probe env, runs its traces and tallies in parallel, and fails on a
// failed build, a program error, a roll that never settled, a tally below the significance level,
// or a run that never reported. Writes artifacts/fairness-probe/summary.{json,md}.
//
// Usage: node scripts/fairness-probe.mjs
//
// Everything above main() is pure and tested by scripts/fairness-probe.test.mjs.

import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

export const ENV = 'native-fairness-probe';
const PROGRAM = join('.pio', 'build', ENV, 'program');

// The test's significance level (ADR-0002 left it to this probe): 0.001.
export const ALPHA = 0.001;
// 2000 traces of each die: enough that a false rest rarer than 1 in 1000 shows up.
export const TRACES = [{ die: 'D20', rolls: 2000, seed: 1 }, { die: 'D6', rolls: 2000, seed: 1 }];
// The fairness test's sizes (99% power against one face 1.25× too likely at ALPHA), each tallied
// with three seeds so the p-values show the spread one fixed seed hides.
export const TALLY_SEEDS = [1, 2, 3];
export const TALLY_SIZES = [
  { die: 'D20', dice: 1, rolls: 18000 },
  { die: 'D20', dice: 3, rolls: 6000 },
  { die: 'D6', dice: 1, rolls: 3400 },
  { die: 'D6', dice: 3, rolls: 1134 },
];
export const FACES = { D6: 6, D20: 20 };

const GRID = /^\[trace-grid\] linear=([\d.,]+) angular=([\d.,]+)$/;
const KEY_VALUES = /(\w+)=(\S+)/g;

function keyValues(line) {
  return Object.fromEntries([...line.matchAll(KEY_VALUES)].map((match) => [match[1], match[2]]));
}

function numbers(text) {
  return text.split(',').map(Number);
}

/** @returns {{kind: string, [key: string]: unknown}} */
export function parseProbeLine(line) {
  const grid = GRID.exec(line);
  if (grid) return { kind: 'grid', linear: numbers(grid[1]), angular: numbers(grid[2]) };
  if (line.startsWith('[trace] ')) {
    const fields = keyValues(line);
    return {
      kind: 'trace',
      die: fields.die,
      finalTiltDeg: Number(fields.final_tilt_deg),
      lastFaceChangeS: Number(fields.last_face_change_s),
      floorLinear: Number(fields.floor_linear),
      floorAngular: Number(fields.floor_angular),
      falseRestS: numbers(fields.false_rest_s),
    };
  }
  if (line.startsWith('[tally] ')) {
    const fields = keyValues(line);
    return {
      kind: 'tally',
      die: fields.die,
      dice: Number(fields.dice),
      rolls: Number(fields.rolls),
      seed: Number(fields.seed),
      counts: numbers(fields.counts),
      nudges: Number(fields.nudges),
      mostNudgesInARoll: Number(fields.most_nudges_in_a_roll),
      unsettled: Number(fields.unsettled),
      onLaunchFace: Number(fields.on_launch_face),
      // Locked results whose die was knocked onto another face before the roll ended.
      knockedOver: Number(fields.knocked_over),
      meanRollS: Number(fields.mean_roll_s),
      longestRollS: Number(fields.longest_roll_s),
      runtimeS: Number(fields.runtime_s),
    };
  }
  throw new Error(`unexpected probe output: ${line}`);
}

// ---------------------------------------------------------------- statistics

const GAMMA_EPSILON = 1e-14;
const GAMMA_MAX_TERMS = 1000;
// Lanczos's approximation to ln Γ, with the coefficients and √(2π) of Numerical Recipes' gammln.
const LANCZOS = [76.18009172947146, -86.50532032941677, 24.01409824083091, -1.231739572450155,
  0.1208650973866179e-2, -0.5395239384953e-5];

function logGamma(x) {
  let denominator = x;
  const t = x + 5.5 - (x + 0.5) * Math.log(x + 5.5);
  let series = 1.000000000190015;
  for (const coefficient of LANCZOS) series += coefficient / ++denominator;
  return -t + Math.log((2.5066282746310005 * series) / x);
}

/** Regularized upper incomplete gamma Q(a, x), by series below a + 1 and continued fraction above. */
function upperGamma(a, x) {
  if (x <= 0) return 1;
  const front = -x + a * Math.log(x) - logGamma(a);
  if (x < a + 1) {
    let term = 1 / a;
    let sum = term;
    for (let n = 1; n < GAMMA_MAX_TERMS && Math.abs(term) > Math.abs(sum) * GAMMA_EPSILON; n++) {
      term *= x / (a + n);
      sum += term;
    }
    return 1 - sum * Math.exp(front);
  }
  // Lentz's method for the continued fraction.
  const tiny = 1e-300;
  let b = x + 1 - a;
  let c = 1 / tiny;
  let d = 1 / b;
  let h = d;
  for (let n = 1; n < GAMMA_MAX_TERMS; n++) {
    const an = -n * (n - a);
    b += 2;
    d = an * d + b;
    if (Math.abs(d) < tiny) d = tiny;
    c = b + an / c;
    if (Math.abs(c) < tiny) c = tiny;
    d = 1 / d;
    const step = d * c;
    h *= step;
    if (Math.abs(step - 1) < GAMMA_EPSILON) break;
  }
  return Math.exp(front) * h;
}

/** The chance of a chi-square statistic at least this large from a fair die. */
export function chiSquarePValue(statistic, degreesOfFreedom) {
  return upperGamma(degreesOfFreedom / 2, statistic / 2);
}

export function chiSquareStatistic(counts) {
  const total = counts.reduce((sum, count) => sum + count, 0);
  const expected = total / counts.length;
  return counts.reduce((sum, count) => sum + (count - expected) ** 2 / expected, 0);
}

// ---------------------------------------------------------------- summaries

function quantile(sorted, fraction) {
  return sorted[Math.min(sorted.length - 1, Math.floor(fraction * sorted.length))];
}

/**
 * The widest gap, by ratio, between neighbouring final tilts above the noise floor: where dice
 * lying flat end and dice propped on something begin.
 */
export function tiltGap(tiltsDeg, noiseFloorDeg = 0.01) {
  const sorted = tiltsDeg.filter((tilt) => tilt > noiseFloorDeg).sort((a, b) => a - b);
  let best = null;
  for (let i = 1; i < sorted.length; i++) {
    const ratio = sorted[i] / sorted[i - 1];
    if (!best || ratio > best.ratio) best = { flatMaxDeg: sorted[i - 1], proppedMinDeg: sorted[i], ratio };
  }
  return best;
}

export function summarizeTraces(parsed) {
  const grid = parsed.find((entry) => entry.kind === 'grid');
  const byDie = {};
  for (const entry of parsed.filter((item) => item.kind === 'trace')) (byDie[entry.die] ??= []).push(entry);
  return Object.entries(byDie).map(([die, traces]) => {
    const lastChanges = traces.map((trace) => trace.lastFaceChangeS).sort((a, b) => a - b);
    const longestFalseRest = grid.linear.map((linear, i) => ({
      linear,
      byAngular: grid.angular.map((angular, j) => ({
        angular,
        seconds: Math.max(...traces.map((trace) => trace.falseRestS[i * grid.angular.length + j])),
      })),
    }));
    return {
      die,
      rolls: traces.length,
      floorLinear: Math.max(...traces.map((trace) => trace.floorLinear)),
      floorAngular: Math.max(...traces.map((trace) => trace.floorAngular)),
      lastFaceChangeS: { median: quantile(lastChanges, 0.5), p99: quantile(lastChanges, 0.99), max: lastChanges.at(-1) },
      tiltGap: tiltGap(traces.map((trace) => trace.finalTiltDeg)),
      longestFalseRest,
    };
  });
}

export function summarizeTally(tally) {
  const faces = FACES[tally.die];
  const results = tally.counts.reduce((sum, count) => sum + count, 0);
  const statistic = chiSquareStatistic(tally.counts);
  return {
    ...tally,
    results,
    statistic,
    pValue: chiSquarePValue(statistic, faces - 1),
    nudgesPerDie: tally.nudges / (tally.rolls * tally.dice),
    onLaunchFaceShare: tally.onLaunchFace / results,
    knockedOverShare: tally.knockedOver / results,
    chanceShare: 1 / faces,
  };
}

/** Every failure across the probe's output: unsettled rolls, unfair tallies, missing runs. */
export function checkRuns(parsed) {
  const failures = [];
  const tallies = parsed.filter((entry) => entry.kind === 'tally');
  for (const tally of tallies) {
    const label = `${tally.die} ×${tally.dice} seed ${tally.seed}`;
    if (tally.counts.length !== FACES[tally.die]) failures.push(`${label}: ${tally.counts.length} counts for ${FACES[tally.die]} faces`);
    if (tally.unsettled > 0) failures.push(`${label}: ${tally.unsettled} rolls never came to rest`);
    const { pValue } = summarizeTally(tally);
    // Written so a NaN, from counts that sum to nothing, fails rather than slipping past.
    if (!(pValue >= ALPHA)) failures.push(`${label}: chi-square p = ${pValue.toExponential(2)}, below ${ALPHA}`);
  }
  for (const size of TALLY_SIZES) {
    for (const seed of TALLY_SEEDS) {
      if (!tallies.some((t) => t.die === size.die && t.dice === size.dice && t.seed === seed && t.rolls === size.rolls)) {
        failures.push(`no tally of ${size.rolls} rolls for ${size.die} ×${size.dice} seed ${seed}`);
      }
    }
  }
  for (const trace of TRACES) {
    const count = parsed.filter((entry) => entry.kind === 'trace' && entry.die === trace.die).length;
    if (count !== trace.rolls) failures.push(`${trace.die}: ${count} traces of ${trace.rolls}`);
  }
  return failures;
}

export function summaryMarkdown(traces, tallies) {
  const lines = ['# Fairness probe', ''];
  for (const trace of traces) {
    lines.push(`## Traces: ${trace.die}, ${trace.rolls} rolls`, '');
    lines.push(`- Resting floor: ${trace.floorLinear} mm/s, ${trace.floorAngular} rad/s (fastest in any roll's last second)`);
    lines.push(`- Last face change: median ${trace.lastFaceChangeS.median} s, 99th percentile ${trace.lastFaceChangeS.p99} s, max ${trace.lastFaceChangeS.max} s`);
    if (trace.tiltGap) {
      lines.push(`- Final tilt gap: flat dice up to ${trace.tiltGap.flatMaxDeg}°, propped dice from ${trace.tiltGap.proppedMinDeg}°`);
    }
    lines.push('', 'Longest pause below the thresholds after which the die still tipped (s):', '');
    lines.push(`| mm/s \\ rad/s | ${trace.longestFalseRest[0].byAngular.map((cell) => cell.angular).join(' | ')} |`);
    lines.push(`|---|${trace.longestFalseRest[0].byAngular.map(() => '---').join('|')}|`);
    for (const row of trace.longestFalseRest) lines.push(`| ${row.linear} | ${row.byAngular.map((cell) => cell.seconds.toFixed(3)).join(' | ')} |`);
    lines.push('');
  }
  lines.push('## Tallies', '');
  lines.push('| Die | Dice | Seed | Results | χ² | p | Nudges per die | Most nudges | On launch face (chance) | Knocked over after locking | Mean / longest roll (s) | Runtime (s) |');
  lines.push('|---|---|---|---|---|---|---|---|---|---|---|---|');
  for (const t of tallies) {
    lines.push(`| ${t.die} | ${t.dice} | ${t.seed} | ${t.results} | ${t.statistic.toFixed(2)} | ${t.pValue.toFixed(3)} | ${t.nudgesPerDie.toFixed(3)} | ${t.mostNudgesInARoll} | ${(100 * t.onLaunchFaceShare).toFixed(1)}% (${(100 * t.chanceShare).toFixed(1)}%) | ${(100 * t.knockedOverShare).toFixed(2)}% | ${t.meanRollS.toFixed(2)} / ${t.longestRollS.toFixed(2)} | ${t.runtimeS.toFixed(1)} |`);
  }
  return `${lines.join('\n')}\n`;
}

// ---------------------------------------------------------------- process handling

function runProgram(args) {
  return new Promise((resolve, reject) => {
    const child = spawn(PROGRAM, args, { stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (chunk) => { stdout += chunk; });
    child.stderr.on('data', (chunk) => { stderr += chunk; });
    child.on('error', (error) => reject(new Error(`could not start ${PROGRAM}: ${error.message}`)));
    child.on('exit', (code, signal) => {
      if (code !== 0) reject(new Error(`${PROGRAM} ${args.join(' ')} exited with code ${code}, signal ${signal}: ${stderr.trim()}`));
      else resolve(stdout.split('\n').filter((line) => line.length > 0).map(parseProbeLine));
    });
  });
}

async function main() {
  const build = spawnSync('pio', ['run', '-e', ENV], { encoding: 'utf8' });
  if (build.status !== 0) throw new Error(`pio run -e ${ENV} failed:\n${build.stdout}${build.stderr}`);

  console.log('Running traces and tallies; about a minute...');
  const runs = [
    ...TRACES.map((t) => ['trace', t.die, String(t.rolls), String(t.seed)]),
    ...TALLY_SIZES.flatMap((size) => TALLY_SEEDS.map((seed) => ['tally', size.die, String(size.dice), String(size.rolls), String(seed)])),
  ];
  const parsed = (await Promise.all(runs.map(runProgram))).flat();
  const failures = checkRuns(parsed);

  const traces = summarizeTraces(parsed);
  const tallies = parsed.filter((entry) => entry.kind === 'tally').map(summarizeTally);
  const outDir = join('artifacts', 'fairness-probe');
  mkdirSync(outDir, { recursive: true });
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ traces, tallies, failures }, null, 2)}\n`);
  writeFileSync(join(outDir, 'summary.md'), summaryMarkdown(traces, tallies));
  console.log(`Artifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${failures.join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: every tally settled and none fell below the significance level');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
