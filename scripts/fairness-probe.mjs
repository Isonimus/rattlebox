#!/usr/bin/env node
// Fairness probe (slice 0008): measures what the rest thresholds, hold time and cocked tolerance
// are chosen from, and tallies rolls of each die type through the real physics for the
// chi-square test of invariant #2.
//
// It also replays the shakes recorded by the IMU probe through the physics, as the device's own
// motion (ADR-0010): how often a die comes back on the face it rested on before the shake, with
// each kick at detection, and chained rolls where every roll starts where the last one ended.
//
// Builds the native-fairness-probe env, runs everything in parallel, and fails on a failed build,
// a program error, a roll that never settled, a shake never detected, a tally or a repeat share
// below the significance level with the product's kick, or a run that never reported. Writes
// artifacts/fairness-probe/summary.{json,md}.
//
// Usage: node scripts/fairness-probe.mjs (about ten minutes on 12 cores)
//
// Everything above main() is pure and tested by scripts/fairness-probe.test.mjs.

import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, readdirSync, writeFileSync } from 'node:fs';
import { availableParallelism } from 'node:os';
import { basename, join } from 'node:path';
import { pathToFileURL } from 'node:url';

export const ENV = 'native-fairness-probe';
const PROGRAM = join('.pio', 'build', ENV, 'program');

// The test's significance level (ADR-0002 left it to this probe): 0.001.
export const ALPHA = 0.001;
// 2000 traces of each die: enough that a false rest rarer than 1 in 1000 shows up.
export const TRACES = [
  { die: 'D20', rolls: 2000, seed: 1 },
  { die: 'D6', rolls: 2000, seed: 1 },
  { die: 'D8', rolls: 2000, seed: 1 },
  { die: 'D10', rolls: 2000, seed: 1 },
];
// The hold must outlast the longest false rest this many times over (ADR-0009).
export const HOLD_MARGIN = 4;
// The fairness test's sizes (99% power against one face 1.25× too likely at ALPHA), each tallied
// with three seeds so the p-values show the spread one fixed seed hides.
export const TALLY_SEEDS = [1, 2, 3];
export const TALLY_SIZES = [
  { die: 'D20', dice: 1, rolls: 18000 },
  { die: 'D20', dice: 3, rolls: 6000 },
  { die: 'D6', dice: 1, rolls: 3400 },
  { die: 'D6', dice: 3, rolls: 1134 },
  // 5,100 results by the same calculation (slice 0026).
  { die: 'D8', dice: 1, rolls: 5100 },
  { die: 'D8', dice: 3, rolls: 1700 },
  // 7,000 results by the same calculation, against 27.877 at 9 degrees of freedom (slice 0027).
  { die: 'D10', dice: 1, rolls: 7000 },
  { die: 'D10', dice: 3, rolls: 2334 },
];
export const FACES = { D6: 6, D8: 8, D10: 10, D20: 20 };
// The chance a die lands on any given face, for the shake report's header.
const CHANCES = Object.entries(FACES).map(([die, faces]) => `${die} ${(100 / faces).toFixed(2)}%`).join(', ');

// The IMU probe's shake takes at the product's 100 Hz (ADR-0005), trimmed to t_us,ax,ay,az.
const SHAKES_DIR = join('test', 'fixtures', 'shakes');
const BOARDS = ['cardputer', 'm5stickc-plus2'];
const TAKE_NUMBERS = [1, 2, 3];
function takes(gesture) {
  return BOARDS.flatMap((board) => TAKE_NUMBERS.map((n) => join(SHAKES_DIR, `${board}-shake-${gesture}-${n}.csv`)));
}
export const GENTLE_TAKES = takes('gentle');
export const HARD_TAKES = takes('hard');
// Other people's takes, as the IMU probe exports them: <board>-p<N>-shake-<kind>-<n>.csv (slice 0023).
const PARTICIPANT_TAKE = /^[a-z0-9-]+-p\d+-shake-[a-z-]+-\d+\.csv$/;
/** The participants' takes among @p files, in name order. */
export function participantTakes(files) {
  return files.filter((file) => PARTICIPANT_TAKE.test(file)).sort().map((file) => join(SHAKES_DIR, file));
}
export const PARTICIPANT_TAKES = participantTakes(readdirSync(SHAKES_DIR));
// The operator's twelve first, in the order ADR-0010's seeds were drawn in, so adding a
// participant's takes leaves those numbers reproducible.
export const ALL_TAKES = [...GENTLE_TAKES, ...HARD_TAKES, ...PARTICIPANT_TAKES];

// What the product does at detection (ADR-0010): a throw, and a 0.1 s spin to a uniformly random
// orientation. Only its runs can fail the probe; the other kicks are the evidence for choosing it.
export const PRODUCT_KICK = 'spin=0.1';
// Each take rolls from its own seed, SHAKE_SEED plus its place in ALL_TAKES: on one seed every take
// would start from the same resting dice, and the takes pooled would not be independent rolls.
export const SHAKE_SEED = 1;
// 1500 rolls of each take, 9000 pooled over six: a standard error of 0.39 points on a D6's
// repeat share of 16.7%.
export const REPLAY_TRIALS = 1500;
// How long the shake goes on after detection before the device is held still (s): right at
// detection is where a gentle shake has had least time to stir the dice.
const CUTS = [0, 0.5, 1];

function replays(dice, kicks, files, cuts) {
  return dice.flatMap((die) => kicks.flatMap((kick) => files.flatMap((file) => cuts.map((cut) => ({
    die, kick, file, cut, seed: SHAKE_SEED + ALL_TAKES.indexOf(file),
  })))));
}

export const REPLAYS = [
  // Gentle shakes slide the dice rather than tumbling them, so they show any kick's leftover pull.
  ...replays(['D6', 'D20'], ['none', 'throw', 'reorient', PRODUCT_KICK], GENTLE_TAKES, CUTS),
  // How short the spin may be, and how long, on the die that keeps its face most.
  ...replays(['D6'], ['spin=0.05', 'spin=0.2', 'spin=0.4'], GENTLE_TAKES, [0]),
  // Hard shakes, with no kick at all.
  ...replays(['D6', 'D20'], ['none'], HARD_TAKES, [0]),
  // Other people's shakes with the product's kick, cut where a shake has stirred least: a kind of
  // shake the spin does not randomize shows as a pool with too many repeats (slice 0023).
  ...replays(['D6', 'D20'], [PRODUCT_KICK], PARTICIPANT_TAKES, [0]),
  // The D8 and the D10 with the product's kick only: the other kicks were evidence for choosing
  // it (slice 0026), and the kick is not in question again for a later die (slice 0027).
  ...replays(['D8', 'D10'], [PRODUCT_KICK], [...GENTLE_TAKES, ...PARTICIPANT_TAKES], [0]),
];

// Rolls one after another, each starting where the last ended, on a random take cut at a random
// time: the tally of invariant #2 as the device rolls. The throw alone is chained too, because its
// results pass chi-square while it repeats the last result far above chance.
export const CHAINS = [
  ...TALLY_SIZES.flatMap((size) => TALLY_SEEDS.map((seed) => ({ ...size, seed, kick: PRODUCT_KICK }))),
  ...TALLY_SIZES.filter((size) => size.die === 'D6').flatMap((size) => TALLY_SEEDS.map((seed) => ({ ...size, seed, kick: 'throw' }))),
];

const GRID =
  /^\[trace-grid\] linear=([\d.,]+) angular=([\d.,]+) product_linear=([\d.]+) product_angular=([\d.]+) product_hold_s=([\d.]+)$/;
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
  if (grid) {
    return {
      kind: 'grid',
      linear: numbers(grid[1]),
      angular: numbers(grid[2]),
      product: { linear: Number(grid[3]), angular: Number(grid[4]), holdS: Number(grid[5]) },
    };
  }
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
  if (line.startsWith('[replay] ')) {
    const fields = keyValues(line);
    return {
      kind: 'replay',
      file: fields.file,
      extraS: Number(fields.extra_s),
      unsettledStarts: Number(fields.unsettled_starts),
      ...shakenFields(fields),
    };
  }
  if (line.startsWith('[chain] ')) {
    const fields = keyValues(line);
    return { kind: 'chain', recordings: Number(fields.recordings), ...shakenFields(fields) };
  }
  throw new Error(`unexpected probe output: ${line}`);
}

function shakenFields(fields) {
  return {
    die: fields.die,
    dice: Number(fields.dice),
    kick: fields.kick,
    seed: Number(fields.seed),
    counts: numbers(fields.counts),
    rolls: Number(fields.rolls),
    results: Number(fields.results),
    // Results on the face their die showed before the shake.
    sameAsBefore: Number(fields.same_as_before),
    undetected: Number(fields.undetected),
    // Rolls the detector started more than once, the shake dipping below its share and back.
    redetected: Number(fields.redetected),
    unsettled: Number(fields.unsettled),
    nudges: Number(fields.nudges),
    knockedOver: Number(fields.knocked_over),
    deepestOutsideMm: Number(fields.deepest_outside_mm),
    fastestMmPerS: Number(fields.fastest_mm_per_s),
    meanRollS: Number(fields.mean_roll_s),
    longestRollS: Number(fields.longest_roll_s),
    runtimeS: Number(fields.runtime_s),
  };
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

/**
 * The chance, two-sided, of a die landing on its previous face this far from 1 in faces times:
 * the normal approximation to the binomial, whose square is chi-square with one degree of freedom.
 */
export function repeatPValue(repeats, results, faces) {
  const chance = 1 / faces;
  const z = (repeats - results * chance) / Math.sqrt(results * chance * (1 - chance));
  return chiSquarePValue(z * z, 1);
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

export function summarizeChain(chain) {
  const faces = FACES[chain.die];
  const statistic = chiSquareStatistic(chain.counts);
  return {
    ...chain,
    statistic,
    pValue: chiSquarePValue(statistic, faces - 1),
    repeatShare: chain.sameAsBefore / chain.results,
    repeatPValue: repeatPValue(chain.sameAsBefore, chain.results, faces),
    nudgesPerDie: chain.nudges / chain.results,
    knockedOverShare: chain.knockedOver / chain.results,
    chanceShare: 1 / faces,
  };
}

/** The kind of shake a take is, from its file name: gentle, hard, one-hand... */
export function gesture(file) {
  return /-shake-([a-z-]+)-\d+\.csv$/.exec(file)?.[1] ?? basename(file);
}

/** Replays pooled over their takes: one entry per die, gesture, kick and cut, in run order. */
export function poolReplays(parsed) {
  const pools = new Map();
  for (const run of parsed.filter((entry) => entry.kind === 'replay')) {
    const key = [run.die, gesture(run.file), run.kick, run.extraS].join('|');
    if (!pools.has(key)) {
      pools.set(key, {
        die: run.die, gesture: gesture(run.file), kick: run.kick, cut: run.extraS,
        takes: 0, results: 0, sameAsBefore: 0, worstTakeShare: 0, redetected: 0, rolls: 0, deepestOutsideMm: 0, fastestMmPerS: 0,
      });
    }
    const pool = pools.get(key);
    pool.takes++;
    pool.results += run.results;
    pool.sameAsBefore += run.sameAsBefore;
    pool.worstTakeShare = Math.max(pool.worstTakeShare, run.sameAsBefore / run.results);
    pool.redetected += run.redetected;
    pool.rolls += run.rolls;
    pool.deepestOutsideMm = Math.max(pool.deepestOutsideMm, run.deepestOutsideMm);
    pool.fastestMmPerS = Math.max(pool.fastestMmPerS, run.fastestMmPerS);
  }
  return [...pools.values()].map((pool) => ({
    ...pool,
    repeatShare: pool.sameAsBefore / pool.results,
    repeatPValue: repeatPValue(pool.sameAsBefore, pool.results, FACES[pool.die]),
    chanceShare: 1 / FACES[pool.die],
  }));
}

function replayLabel(run) {
  return `${run.die} ${run.kick} cut ${run.extraS ?? run.cut} s ${basename(run.file)}`;
}

function chainLabel(chain) {
  return `chain ${chain.die} ×${chain.dice} ${chain.kick} seed ${chain.seed}`;
}

/** Shaken runs' failures: a shake never detected, a roll never settled, or a die short of faces. */
function checkShakenRuns(runs, label) {
  const failures = [];
  for (const run of runs) {
    if (run.counts.length !== FACES[run.die]) failures.push(`${label(run)}: ${run.counts.length} counts for ${FACES[run.die]} faces`);
    if (run.undetected > 0) failures.push(`${label(run)}: ${run.undetected} shakes never detected`);
    if (run.unsettled > 0) failures.push(`${label(run)}: ${run.unsettled} rolls never came to rest`);
    if (run.unsettledStarts > 0) failures.push(`${label(run)}: ${run.unsettledStarts} launches before the shake never came to rest`);
  }
  return failures;
}

/**
 * A failure for each die that paused at rest by the product's thresholds and then tipped, for longer
 * than the hold allows with ADR-0009's margin: a hold that short could lock a face the die then left.
 */
function checkHold(parsed) {
  const failures = [];
  // Each die's run prints its own grid line before its traces. The hold on that line is the hold
  // this die locks by, which is not the hold any other die locks by (ADR-0028), so a trace is
  // only ever checked against the grid its own run printed.
  const gridOf = new Map();
  let latest = null;
  for (const entry of parsed) {
    if (entry.kind === 'grid') latest = entry;
    if (entry.kind !== 'trace') continue;
    if (latest === null) {
      failures.push(`${entry.die} trace: no trace grid before it`);
      continue;
    }
    const earlier = gridOf.get(entry.die);
    if (earlier !== undefined && earlier.product.holdS !== latest.product.holdS) {
      failures.push(`${entry.die}: traced against two holds, ${earlier.product.holdS} s and ${latest.product.holdS} s`);
    }
    const points = latest.linear.length * latest.angular.length;
    if (entry.falseRestS.length !== points) {
      failures.push(`${entry.die} trace: ${entry.falseRestS.length} false rests for ${points} grid points`);
    }
    gridOf.set(entry.die, latest);
  }
  for (const die of summarizeTraces(parsed)) {
    const grid = gridOf.get(die.die);
    if (grid === undefined) continue;
    const { linear, angular, holdS } = grid.product;
    const seconds = die.longestFalseRest.find((row) => row.linear === linear)?.byAngular.find((cell) => cell.angular === angular)?.seconds;
    if (seconds === undefined) {
      failures.push(`${die.die}: the trace grid has no point at the product's ${linear} mm/s and ${angular} rad/s`);
      continue;
    }
    // Written so a NaN, from a value missing or unreadable, fails rather than slipping past.
    if (!(seconds * HOLD_MARGIN <= holdS)) {
      failures.push(`${die.die}: a false rest of ${seconds} s at ${linear} mm/s and ${angular} rad/s, which the ${holdS} s hold does not outlast ${HOLD_MARGIN} times over`);
    }
  }
  return failures;
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
  failures.push(...checkHold(parsed));

  const replayRuns = parsed.filter((entry) => entry.kind === 'replay');
  const chains = parsed.filter((entry) => entry.kind === 'chain');
  failures.push(...checkShakenRuns(replayRuns, replayLabel), ...checkShakenRuns(chains, chainLabel));
  // Written so a NaN, from no results, fails rather than slipping past.
  for (const chain of chains.filter((entry) => entry.kick === PRODUCT_KICK).map(summarizeChain)) {
    if (!(chain.pValue >= ALPHA)) failures.push(`${chainLabel(chain)}: chi-square p = ${chain.pValue.toExponential(2)}, below ${ALPHA}`);
    if (!(chain.repeatPValue >= ALPHA)) {
      failures.push(`${chainLabel(chain)}: ${(100 * chain.repeatShare).toFixed(2)}% on the previous face, p = ${chain.repeatPValue.toExponential(2)}, below ${ALPHA}`);
    }
  }
  for (const pool of poolReplays(parsed).filter((entry) => entry.kick === PRODUCT_KICK)) {
    if (!(pool.repeatPValue >= ALPHA)) {
      failures.push(`${pool.die} ${pool.kick} ${pool.gesture} cut ${pool.cut} s: ${(100 * pool.repeatShare).toFixed(2)}% on the previous face, p = ${pool.repeatPValue.toExponential(2)}, below ${ALPHA}`);
    }
  }
  for (const planned of REPLAYS) {
    if (!replayRuns.some((r) => r.die === planned.die && r.kick === planned.kick && r.file === planned.file && r.extraS === planned.cut && r.seed === planned.seed && r.rolls === REPLAY_TRIALS)) {
      failures.push(`no replay of ${REPLAY_TRIALS} rolls for ${replayLabel(planned)}`);
    }
  }
  for (const planned of CHAINS) {
    if (!chains.some((c) => c.die === planned.die && c.dice === planned.dice && c.kick === planned.kick && c.seed === planned.seed && c.rolls === planned.rolls)) {
      failures.push(`no chain of ${planned.rolls} rolls for ${chainLabel(planned)}`);
    }
  }
  return failures;
}

function percent(share) {
  return `${(100 * share).toFixed(2)}%`;
}

export function shakeMarkdown(pools, chains) {
  const lines = ['## Shakes: results on the face the die showed before', ''];
  lines.push(`One die, ${REPLAY_TRIALS} rolls per take, pooled over the takes; the cut is how long the shake went on after detection before the device was held still. Chance: ${CHANCES}. p is two-sided, against chance.`, '');
  const cuts = [...new Set(pools.map((pool) => pool.cut))].sort((a, b) => a - b);
  lines.push(`| Die | Shakes | Kick | ${cuts.map((cut) => `Cut ${cut} s: pooled (worst take), p`).join(' | ')} |`);
  lines.push(`|---|---|---|${cuts.map(() => '---').join('|')}|`);
  const rows = new Map();
  for (const pool of pools) {
    const key = [pool.die, pool.gesture, pool.kick].join('|');
    if (!rows.has(key)) rows.set(key, { pool, byCut: new Map() });
    rows.get(key).byCut.set(pool.cut, pool);
  }
  for (const { pool, byCut } of rows.values()) {
    const cells = cuts.map((cut) => {
      const cell = byCut.get(cut);
      return cell ? `${percent(cell.repeatShare)} (${percent(cell.worstTakeShare)}), ${cell.repeatPValue.toFixed(3)}` : '';
    });
    lines.push(`| ${pool.die} | ${pool.gesture} | ${pool.kick} | ${cells.join(' | ')} |`);
  }
  const product = pools.filter((pool) => pool.kick === PRODUCT_KICK);
  if (product.length > 0) {
    lines.push('', `With ${PRODUCT_KICK}: deepest vertex past a wall ${Math.max(...product.map((p) => p.deepestOutsideMm)).toFixed(4)} mm, fastest die ${Math.max(...product.map((p) => p.fastestMmPerS)).toFixed(0)} mm/s, ${product.reduce((sum, p) => sum + p.redetected, 0)} of ${product.reduce((sum, p) => sum + p.rolls, 0)} rolls detected more than once.`);
  }
  lines.push('', '## Shakes: chained rolls', '');
  lines.push('| Die | Dice | Kick | Seed | Results | χ² | p | On the previous face (chance) | p | Detected more than once | Nudges per die | Knocked over after locking | Mean / longest roll from detection (s) | Deepest past a wall (mm) | Fastest (mm/s) | Runtime (s) |');
  lines.push('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|');
  for (const c of chains) {
    lines.push(`| ${c.die} | ${c.dice} | ${c.kick} | ${c.seed} | ${c.results} | ${c.statistic.toFixed(2)} | ${c.pValue.toFixed(3)} | ${percent(c.repeatShare)} (${percent(c.chanceShare)}) | ${c.repeatPValue.toFixed(3)} | ${c.redetected} of ${c.rolls} | ${c.nudgesPerDie.toFixed(3)} | ${percent(c.knockedOverShare)} | ${c.meanRollS.toFixed(2)} / ${c.longestRollS.toFixed(2)} | ${c.deepestOutsideMm.toFixed(4)} | ${c.fastestMmPerS.toFixed(0)} | ${c.runtimeS.toFixed(1)} |`);
  }
  return `${lines.join('\n')}\n`;
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

/**
 * Run every set of arguments through @p run, at most @p limit at once, and return their parsed lines
 * in run order. The first failure stops new runs from starting, so it is reported in seconds rather
 * than after the rest of the queue.
 */
export async function runAll(runs, limit, run) {
  const outputs = new Array(runs.length);
  let next = 0;
  let failed = false;
  async function worker() {
    while (!failed && next < runs.length) {
      const index = next++;
      try {
        outputs[index] = await run(runs[index]);
      } catch (error) {
        failed = true;
        throw error;
      }
    }
  }
  await Promise.all(Array.from({ length: Math.min(limit, runs.length) }, worker));
  return outputs.flat();
}

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

  console.log('Running traces, tallies, replays and chains; about ten minutes on 12 cores...');
  // The chains are the longest runs, so they start first rather than finishing last alone.
  const runs = [
    ...CHAINS.map((c) => ['chain', c.die, String(c.dice), String(c.rolls), String(c.seed), c.kick, ...ALL_TAKES]),
    ...TRACES.map((t) => ['trace', t.die, String(t.rolls), String(t.seed)]),
    ...TALLY_SIZES.flatMap((size) => TALLY_SEEDS.map((seed) => ['tally', size.die, String(size.dice), String(size.rolls), String(seed)])),
    ...REPLAYS.map((r) => ['replay', r.die, '1', String(REPLAY_TRIALS), String(r.seed), r.kick, String(r.cut), r.file]),
  ];
  const parsed = await runAll(runs, availableParallelism(), runProgram);
  const failures = checkRuns(parsed);

  const traces = summarizeTraces(parsed);
  const tallies = parsed.filter((entry) => entry.kind === 'tally').map(summarizeTally);
  const pools = poolReplays(parsed);
  const chains = parsed.filter((entry) => entry.kind === 'chain').map(summarizeChain);
  const outDir = join('artifacts', 'fairness-probe');
  mkdirSync(outDir, { recursive: true });
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ traces, tallies, pools, chains, failures }, null, 2)}\n`);
  writeFileSync(join(outDir, 'summary.md'), `${summaryMarkdown(traces, tallies)}\n${shakeMarkdown(pools, chains)}`);
  console.log(`Artifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${failures.join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: every roll settled, every shake was detected, and no check fell below the significance level');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
