// Tests for the pure half of scripts/fairness-probe.mjs (slice 0008), on lines in the form the
// probe program prints them.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { join } from 'node:path';

import {
  ALL_TAKES, CHAINS, FACES, GENTLE_TAKES, HARD_TAKES, PRODUCT_KICK, REPLAYS, REPLAY_TRIALS, TALLY_SEEDS, TALLY_SIZES, TRACES, checkRuns,
  chiSquarePValue, chiSquareStatistic, gesture, parseProbeLine, participantTakes, poolReplays, repeatPValue, runAll, shakeMarkdown,
  summarizeChain, summarizeTally, summarizeTraces, summaryMarkdown, tiltGap,
} from './fairness-probe.mjs';

const GRID = '[trace-grid] linear=1,5 angular=0.1,0.5 product_linear=5 product_angular=0.5 product_hold_s=0.15';
// The product's per-die holds (src/geometry/die_shape.cpp, ADR-0028). Each die's run prints its
// own grid line, so a fixture of a whole run has one per die and not one for all of them.
const HOLD_S = { D6: 0.01, D8: 0.01, D10: 0.07, D12: 0.04, D20: 0.04 };
const gridFor = (die) =>
  `[trace-grid] linear=1,5 angular=0.1,0.5 product_linear=5 product_angular=0.1 product_hold_s=${HOLD_S[die]}`;
// A pause well inside what that die's hold outlasts four times over.
const restsFor = (die) => `0.0000,0.0000,0.0000,${(HOLD_S[die] / 5).toFixed(4)}`;

function trace(die, tilt, falseRest = '0.0000,0.0100,0.0200,0.0370') {
  return `[trace] die=${die} roll=0 final_tilt_deg=${tilt} last_face_change_s=0.2500 floor_linear=0.000150 floor_angular=0.000020 false_rest_s=${falseRest}`;
}

function tally(die, dice, seed, counts, extra = {}) {
  const fields = { nudges: 10, most_nudges_in_a_roll: 2, unsettled: 0, on_launch_face: 100, knocked_over: 0, mean_roll_s: '0.5', longest_roll_s: '1.5', runtime_s: '2.0', ...extra };
  const rest = Object.entries(fields).map(([key, value]) => `${key}=${value}`).join(' ');
  const rolls = TALLY_SIZES.find((size) => size.die === die && size.dice === dice)?.rolls ?? 100;
  return `[tally] die=${die} dice=${dice} rolls=${rolls} seed=${seed} counts=${counts.join(',')} ${rest}`;
}

const FAIR_D20 = Array(20).fill(900);
const FAIR_D6 = Array(6).fill(567);

/** @p results spread as evenly as whole counts allow over @p faces. */
function even(results, faces) {
  return Array.from({ length: faces }, (_, face) => Math.floor(results / faces) + (face < results % faces ? 1 : 0));
}

/** The fields replay and chain lines share, for @p results results of which @p same repeat. */
function shakenTail(die, rolls, results, same, extra) {
  const fields = { rolls, results, same_as_before: same, undetected: 0, redetected: 3, unsettled: 0, nudges: 6, knocked_over: 0, deepest_outside_mm: '0.0100', fastest_mm_per_s: '4000.0', mean_roll_s: '3.1', longest_roll_s: '6.0', runtime_s: '9.0', ...extra };
  return `counts=${even(results, FACES[die]).join(',')} ${Object.entries(fields).map(([key, value]) => `${key}=${value}`).join(' ')}`;
}

function replay({ die, kick, file, cut, seed }, same = REPLAY_TRIALS / FACES[die], extra = {}) {
  return `[replay] file=${file} die=${die} dice=1 kick=${kick} extra_s=${cut.toFixed(3)} seed=${seed} unsettled_starts=0 ${shakenTail(die, REPLAY_TRIALS, REPLAY_TRIALS, same, extra)}`;
}

function chain({ die, dice, rolls, seed, kick }, same = Math.round((rolls * dice) / FACES[die]), extra = {}) {
  return `[chain] die=${die} dice=${dice} kick=${kick} seed=${seed} recordings=12 ${shakenTail(die, rolls, rolls * dice, same, extra)}`;
}

/** A complete, passing run: every trace, tally, replay and chain the probe runs. */
function completeRun() {
  const lines = [];
  for (const t of TRACES) {
    lines.push(gridFor(t.die));
    for (let i = 0; i < t.rolls; i++) lines.push(trace(t.die, '0.0000', restsFor(t.die)));
  }
  for (const size of TALLY_SIZES) {
    for (const seed of TALLY_SEEDS) lines.push(tally(size.die, size.dice, seed, even(size.rolls * size.dice, FACES[size.die])));
  }
  for (const planned of REPLAYS) lines.push(replay(planned));
  for (const planned of CHAINS) lines.push(chain(planned));
  return lines.map(parseProbeLine);
}

const PRODUCT_D6_CHAIN = CHAINS.find((c) => c.kick === PRODUCT_KICK && c.die === 'D6' && c.dice === 1 && c.seed === 1);

/** @p run with its first entry matching @p match parsed from @p line instead. */
function replacing(run, match, line) {
  const index = run.findIndex(match);
  assert.ok(index >= 0, 'nothing to replace');
  return run.map((entry, i) => (i === index ? parseProbeLine(line) : entry));
}

function isPlannedChain(planned) {
  return (entry) => entry.kind === 'chain' && entry.die === planned.die && entry.dice === planned.dice && entry.kick === planned.kick && entry.seed === planned.seed;
}

test('the chi-square p-value matches tabulated points', () => {
  // Upper 0.1% points: 43.820 at 19 degrees of freedom, 20.515 at 5; and Q(1, 1) = e⁻¹ at 2.
  assert.ok(Math.abs(chiSquarePValue(43.820, 19) - 0.001) < 1e-5);
  assert.ok(Math.abs(chiSquarePValue(20.515, 5) - 0.001) < 1e-5);
  assert.ok(Math.abs(chiSquarePValue(2, 2) - Math.exp(-1)) < 1e-12);
  assert.equal(chiSquarePValue(0, 5), 1);
});

test('the chi-square statistic matches a worked example', () => {
  assert.ok(Math.abs(chiSquareStatistic([5, 8, 9, 8, 10, 20]) - 13.4) < 1e-12);
});

test('trace, grid and tally lines parse into their fields', () => {
  assert.deepEqual(parseProbeLine(GRID), {
    kind: 'grid', linear: [1, 5], angular: [0.1, 0.5], product: { linear: 5, angular: 0.5, holdS: 0.15 },
  });
  const parsed = parseProbeLine(trace('D20', '12.5000'));
  assert.equal(parsed.kind, 'trace');
  assert.equal(parsed.finalTiltDeg, 12.5);
  assert.deepEqual(parsed.falseRestS, [0, 0.01, 0.02, 0.037]);
  const counted = parseProbeLine(tally('D6', 3, 2, FAIR_D6, { unsettled: 4 }));
  assert.equal(counted.dice, 3);
  assert.equal(counted.seed, 2);
  assert.equal(counted.unsettled, 4);
  assert.deepEqual(counted.counts, FAIR_D6);
});

test('an unexpected line is refused rather than ignored', () => {
  assert.throws(() => parseProbeLine('usage: fairness_probe trace'), /unexpected probe output/);
});

test('the tilt gap separates dice lying flat from dice propped up', () => {
  assert.deepEqual(tiltGap([0, 0.03, 0.07, 0.229, 1.085, 5, 20]), { flatMaxDeg: 0.229, proppedMinDeg: 1.085, ratio: 1.085 / 0.229 });
  assert.equal(tiltGap([0, 0, 0.001]), null);
});

test('traces summarise to the worst false rest per threshold pair', () => {
  const parsed = [GRID, trace('D20', '0', '0,0,0,0.0370'), trace('D20', '1.2', '0,0.0500,0,0.0100')].map(parseProbeLine);
  const [summary] = summarizeTraces(parsed);
  assert.equal(summary.rolls, 2);
  assert.deepEqual(summary.longestFalseRest[0].byAngular.map((cell) => cell.seconds), [0, 0.05]);
  assert.deepEqual(summary.longestFalseRest[1].byAngular.map((cell) => cell.seconds), [0, 0.037]);
});

test('a complete fair run passes', () => {
  assert.deepEqual(checkRuns(completeRun()), []);
});

test('an unsettled roll, an unfair tally, a missing tally and missing traces are each named', () => {
  const run = completeRun();
  const unsettled = run.map((entry) => (entry.kind === 'tally' && entry.die === 'D6' && entry.dice === 1 && entry.seed === 1 ? { ...entry, unsettled: 3 } : entry));
  assert.deepEqual(checkRuns(unsettled), ['D6 ×1 seed 1: 3 rolls never came to rest']);

  const loaded = [...FAIR_D20];
  loaded[0] = 1300;
  loaded[19] = 500;
  const unfair = run.map((entry) => (entry.kind === 'tally' && entry.die === 'D20' && entry.dice === 3 && entry.seed === 2 ? { ...entry, counts: loaded } : entry));
  assert.equal(checkRuns(unfair).length, 1);
  assert.match(checkRuns(unfair)[0], /^D20 ×3 seed 2: chi-square p = .* below 0.001$/);

  const missingTally = run.filter((entry) => !(entry.kind === 'tally' && entry.die === 'D20' && entry.dice === 1 && entry.seed === 3));
  assert.deepEqual(checkRuns(missingTally), ['no tally of 18000 rolls for D20 ×1 seed 3']);
  const shortTally = run.map((entry) => (entry.kind === 'tally' && entry.die === 'D20' && entry.dice === 1 && entry.seed === 3 ? { ...entry, rolls: 1 } : entry));
  assert.ok(checkRuns(shortTally).includes('no tally of 18000 rolls for D20 ×1 seed 3'));

  const firstD6Trace = run.findIndex((entry) => entry.kind === 'trace' && entry.die === 'D6');
  const missingTrace = run.filter((_, i) => i !== firstD6Trace);
  assert.deepEqual(checkRuns(missingTrace), [`D6: ${TRACES[1].rolls - 1} traces of ${TRACES[1].rolls}`]);
});

/** @p run with @p die's first trace replaced by one whose longest false rest is @p seconds. */
function tracingAFalseRestOf(run, die, seconds) {
  const first = run.findIndex((entry) => entry.kind === 'trace' && entry.die === die);
  assert.ok(first >= 0, `no ${die} trace to replace`);
  // The grid is linear 1,5 outer and angular 0.1,0.5 inner, so the product's 5 mm/s and
  // 0.1 rad/s is the third of the four. A looser threshold can only find a pause at least as
  // long, so the fourth carries it too.
  return run.map((entry, i) => (i === first ? parseProbeLine(trace(die, '0.0000', `0.0000,0.0000,${seconds},${seconds}`)) : entry));
}

/** @p run with @p die's grid line carrying @p holdS instead of the hold it ships with. */
function holding(run, die, holdS) {
  return run.map((entry) =>
    entry.kind === 'grid' && entry.product.holdS === HOLD_S[die] ? { ...entry, product: { ...entry.product, holdS } } : entry,
  );
}

test('a false rest the hold does not outlast four times over fails: the D10 traced 0.0167 s, which 0.07 s covers and 0.04 s does not', () => {
  const run = tracingAFalseRestOf(completeRun(), 'D10', '0.0167');
  assert.deepEqual(checkRuns(run), []);
  // 0.04 s is what the D12 and the D20 hold for, and it is exactly what the D10's trace refuses:
  // the hold is the die's own, and the longest pause differs between dice (ADR-0028, ADR-0031).
  assert.deepEqual(checkRuns(holding(run, 'D10', 0.04)), [
    'D10: a false rest of 0.0167 s at 5 mm/s and 0.1 rad/s, which the 0.04 s hold does not outlast 4 times over',
  ]);
});

test('a die traced against two different holds is reported, not silently given the last one', () => {
  // Cannot happen while each die has one trace run, which is why it is worth a test: a second
  // run added later at a stale hold would otherwise overwrite the first and check nothing.
  const run = completeRun();
  const firstD10 = run.findIndex((entry) => entry.kind === 'trace' && entry.die === 'D10');
  const stale = parseProbeLine(gridFor('D10').replace(`product_hold_s=${HOLD_S.D10}`, 'product_hold_s=0.04'));
  const withTwo = [...run.slice(0, firstD10 + 1), stale, run[firstD10], ...run.slice(firstD10 + 1)];
  assert.ok(checkRuns(withTwo).includes('D10: traced against two holds, 0.07 s and 0.04 s'));
});

test('each die is checked against its own hold, not the longest hold in the run', () => {
  // 0.0167 s is fine for the D10, whose hold is 0.07 s, and far too long for the D6, whose hold
  // is 0.01 s. Before the hold was per die, one number covered both and this pause passed for both.
  const run = tracingAFalseRestOf(completeRun(), 'D6', '0.0167');
  assert.deepEqual(checkRuns(run), [
    'D6: a false rest of 0.0167 s at 5 mm/s and 0.1 rad/s, which the 0.01 s hold does not outlast 4 times over',
  ]);
  assert.deepEqual(checkRuns(tracingAFalseRestOf(completeRun(), 'D10', '0.0167')), []);
});

test('a trace short of a false rest per grid point is named, not read as a pause of NaN that passes the hold', () => {
  const run = completeRun();
  const firstD8Trace = run.findIndex((entry) => entry.kind === 'trace' && entry.die === 'D8');
  run[firstD8Trace] = parseProbeLine(trace('D8', '0.0000', '0.0000,0.0100'));
  const failures = checkRuns(run);
  assert.ok(failures.includes('D8 trace: 2 false rests for 4 grid points'));
  assert.ok(failures.some((failure) => /^D8: a false rest of NaN s/.test(failure)));
});

test('a grid that leaves out the product\'s thresholds fails rather than checking the hold against nothing', () => {
  const run = completeRun().map((entry) => (entry.kind === 'grid' ? { ...entry, product: { ...entry.product, linear: 2 } } : entry));
  // Each die is named, because each die's run printed the grid its own hold was checked against.
  assert.deepEqual(
    checkRuns(run),
    TRACES.map((t) => `${t.die}: the trace grid has no point at the product's 2 mm/s and 0.1 rad/s`),
  );
});

test('a tally whose counts sum to nothing fails rather than passing on a NaN', () => {
  const run = completeRun().map((entry) => (entry.kind === 'tally' && entry.die === 'D6' && entry.dice === 1 && entry.seed === 2 ? { ...entry, counts: [0, 0, 0, 0, 0, 0] } : entry));
  assert.ok(checkRuns(run).some((failure) => /^D6 ×1 seed 2: chi-square p = NaN/.test(failure)));
});

test('a tally with the wrong number of faces is named', () => {
  const run = completeRun().map((entry) => (entry.kind === 'tally' && entry.die === 'D6' && entry.dice === 3 && entry.seed === 3 ? { ...entry, counts: FAIR_D20 } : entry));
  assert.ok(checkRuns(run).includes('D6 ×3 seed 3: 20 counts for 6 faces'));
});

test('the summary reports shares against chance and a row per tally', () => {
  const summary = summarizeTally(parseProbeLine(tally('D20', 1, 1, FAIR_D20, { on_launch_face: 900, nudges: 4500, knocked_over: 18 })));
  assert.equal(summary.knockedOverShare, 0.001);
  assert.equal(summary.results, 18000);
  assert.equal(summary.statistic, 0);
  assert.equal(summary.pValue, 1);
  assert.equal(summary.onLaunchFaceShare, 0.05);
  assert.equal(summary.chanceShare, 0.05);
  assert.equal(summary.nudgesPerDie, 0.25);
  const markdown = summaryMarkdown(summarizeTraces([GRID, trace('D20', '0')].map(parseProbeLine)), [summary]);
  assert.match(markdown, /\| D20 \| 1 \| 1 \| 18000 \| 0\.00 \| 1\.000 \| 0\.250 \| 2 \| 5\.0% \(5\.0%\) \| 0\.10% \|/);
});

test('the repeat p-value matches two-sided points of the normal distribution', () => {
  // z = 1.96 leaves 5%, z = 3.2905 leaves 0.1%; n = 3600 at 1 in 6 has a standard deviation of 22.36.
  const sd = Math.sqrt(3600 * (1 / 6) * (5 / 6));
  assert.ok(Math.abs(repeatPValue(600 + 1.96 * sd, 3600, 6) - 0.05) < 1e-4);
  assert.ok(Math.abs(repeatPValue(600 - 3.2905 * sd, 3600, 6) - 0.001) < 1e-5);
  assert.equal(repeatPValue(600, 3600, 6), 1);
  assert.ok(Number.isNaN(repeatPValue(0, 0, 6)));
});

test('replay and chain lines parse into their fields', () => {
  const planned = REPLAYS.find((r) => r.kick === PRODUCT_KICK && r.die === 'D20');
  const parsed = parseProbeLine(replay(planned, 80, { redetected: 7 }));
  assert.equal(parsed.kind, 'replay');
  assert.equal(parsed.file, planned.file);
  assert.equal(parsed.kick, PRODUCT_KICK);
  assert.equal(parsed.extraS, planned.cut);
  assert.equal(parsed.sameAsBefore, 80);
  assert.equal(parsed.redetected, 7);
  assert.equal(parsed.counts.length, 20);
  const chained = parseProbeLine(chain(PRODUCT_D6_CHAIN, 700));
  assert.equal(chained.kind, 'chain');
  assert.equal(chained.recordings, 12);
  assert.equal(chained.results, 3400);
  assert.equal(chained.sameAsBefore, 700);
});

test('a product chain that repeats the last result too often fails, though chi-square passes', () => {
  // 24% repeats on a D6 with even counts: the bias a throw alone left, which chi-square cannot see.
  const run = replacing(completeRun(), isPlannedChain(PRODUCT_D6_CHAIN), chain(PRODUCT_D6_CHAIN, 816));
  const failures = checkRuns(run);
  assert.equal(failures.length, 1);
  assert.match(failures[0], /^chain D6 ×1 spin=0\.1 seed 1: 24\.00% on the previous face, p = .* below 0\.001$/);
});

test('a product chain whose results are unfair fails', () => {
  const loaded = `[chain] die=D6 dice=1 kick=${PRODUCT_KICK} seed=1 recordings=12 counts=800,520,520,520,520,520 ${shakenTail('D6', 3400, 3400, 567, {}).replace(/^counts=\S+ /, '')}`;
  const failures = checkRuns(replacing(completeRun(), isPlannedChain(PRODUCT_D6_CHAIN), loaded));
  assert.equal(failures.length, 1);
  assert.match(failures[0], /^chain D6 ×1 spin=0\.1 seed 1: chi-square p = .* below 0\.001$/);
});

test('a throw chain that repeats far above chance is reported, not failed', () => {
  const throwChain = CHAINS.find((c) => c.kick === 'throw');
  assert.deepEqual(checkRuns(replacing(completeRun(), isPlannedChain(throwChain), chain(throwChain, 900))), []);
});

test('product replays pooled over their takes fail when they keep the previous face', () => {
  // Every gentle take, the operator's and the participants': they pool together as one kind.
  const planned = REPLAYS.filter((r) => r.kick === PRODUCT_KICK && r.die === 'D6' && r.cut === 0 && gesture(r.file) === 'gentle');
  let run = completeRun();
  // 20% on every take, over 3 points above chance: far past 0.001 even for one take of 1500.
  for (const take of planned) {
    run = replacing(run, (e) => e.kind === 'replay' && e.die === take.die && e.kick === take.kick && e.file === take.file && e.extraS === take.cut, replay(take, 300));
  }
  const failures = checkRuns(run);
  assert.equal(failures.length, 1);
  assert.match(failures[0], /^D6 spin=0\.1 gentle cut 0 s: 20\.00% on the previous face, p = .* below 0\.001$/);
});

test('a shake never detected, a roll never settled and a missing replay or chain are each named', () => {
  const take = REPLAYS[0];
  const isTake = (e) => e.kind === 'replay' && e.die === take.die && e.kick === take.kick && e.file === take.file && e.extraS === take.cut;
  const label = `${take.die} ${take.kick} cut ${take.cut} s ${take.file.split('/').at(-1)}`;
  assert.deepEqual(checkRuns(replacing(completeRun(), isTake, replay(take, undefined, { undetected: 2 }))), [`${label}: 2 shakes never detected`]);
  assert.deepEqual(checkRuns(replacing(completeRun(), isTake, replay(take, undefined, { unsettled: 1 }))), [`${label}: 1 rolls never came to rest`]);
  assert.deepEqual(checkRuns(completeRun().filter((e) => !isTake(e))), [`no replay of ${REPLAY_TRIALS} rolls for ${label}`]);
  assert.deepEqual(checkRuns(completeRun().filter((e) => !isPlannedChain(PRODUCT_D6_CHAIN)(e))), ['no chain of 3400 rolls for chain D6 ×1 spin=0.1 seed 1']);
});

test('replays pool per die, gesture, kick and cut, keeping the worst take', () => {
  const planned = REPLAYS.filter((r) => r.kick === 'none' && r.die === 'D6' && r.cut === 0 && r.file.includes('gentle'));
  const lines = planned.map((take, i) => replay(take, i === 0 ? 1350 : 1200));
  const [pool] = poolReplays(lines.map(parseProbeLine));
  assert.equal(pool.takes, 6);
  assert.equal(pool.gesture, 'gentle');
  assert.equal(pool.results, 9000);
  assert.equal(pool.repeatShare, 7350 / 9000);
  assert.equal(pool.worstTakeShare, 0.9);
  const markdown = shakeMarkdown(poolReplays(lines.map(parseProbeLine)), [summarizeChain(parseProbeLine(chain(PRODUCT_D6_CHAIN, 567)))]);
  assert.match(markdown, /\| D6 \| gentle \| none \| 81\.67% \(90\.00%\), 0\.000 \|/);
  assert.match(markdown, /\| D6 \| 1 \| spin=0\.1 \| 1 \| 3400 \| [\d.]+ \| [\d.]+ \| 16\.68% \(16\.67%\) \| 0\.988 \| 3 of 3400 \|/);
  // Every die the replays roll has its chance in the header, the D8 included (slice 0026).
  assert.match(markdown, /Chance: D6 16\.67%, D8 12\.50%, D10 10\.00%, D12 8\.33%, D20 5\.00%\./);
});

test('each take of a pooled replay rolls from its own seed', () => {
  const pooled = REPLAYS.filter((r) => r.kick === PRODUCT_KICK && r.die === 'D6' && r.cut === 0);
  assert.equal(new Set(pooled.map((r) => r.seed)).size, pooled.length);
  const run = completeRun();
  const take = pooled[1];
  const isTake = (e) => e.kind === 'replay' && e.die === take.die && e.kick === take.kick && e.file === take.file && e.extraS === take.cut;
  // The same take on the first take's seed is not the run that was planned.
  const onFirstSeed = run.map((e) => (isTake(e) ? { ...e, seed: pooled[0].seed } : e));
  assert.equal(checkRuns(onFirstSeed).length, 1);
  assert.equal(ALL_TAKES.indexOf(take.file), take.seed - 1);
});

test('participants\' takes come after the operator\'s twelve, whose seeds stay as they were', () => {
  const files = ['m5stickc-plus2-p3-shake-twist-2.csv', 'cardputer-shake-gentle-1.csv', 'cardputer-p2-shake-one-hand-1.csv', 'notes.md', 'cardputer-p2-walk-1.csv'];
  assert.deepEqual(participantTakes(files), [
    join('test', 'fixtures', 'shakes', 'cardputer-p2-shake-one-hand-1.csv'),
    join('test', 'fixtures', 'shakes', 'm5stickc-plus2-p3-shake-twist-2.csv'),
  ]);
  // ADR-0010's numbers were drawn with these seeds: the gentle takes 1-6 and the hard 7-12.
  assert.deepEqual(ALL_TAKES.slice(0, 12), [...GENTLE_TAKES, ...HARD_TAKES]);
  assert.equal(GENTLE_TAKES[0], join('test', 'fixtures', 'shakes', 'cardputer-shake-gentle-1.csv'));
  assert.equal(HARD_TAKES[5], join('test', 'fixtures', 'shakes', 'm5stickc-plus2-shake-hard-3.csv'));
});

test('a take\'s kind of shake reads from its name, hyphens included', () => {
  assert.equal(gesture(join('test', 'fixtures', 'shakes', 'cardputer-shake-gentle-2.csv')), 'gentle');
  assert.equal(gesture(join('test', 'fixtures', 'shakes', 'cardputer-p2-shake-one-hand-1.csv')), 'one-hand');
  assert.equal(gesture(join('test', 'fixtures', 'shakes', 'm5stickc-plus2-p3-shake-twist-2.csv')), 'twist');
});

test('every participant take is replayed with the product\'s kick at detection, on both dice', () => {
  const participants = ALL_TAKES.slice(12);
  for (const file of participants) {
    for (const die of ['D6', 'D20']) {
      assert.ok(REPLAYS.some((r) => r.file === file && r.die === die && r.kick === PRODUCT_KICK && r.cut === 0), `${die} ${file}`);
    }
  }
  // Only the product's kick: the kick comparison stays on the operator's takes.
  assert.ok(REPLAYS.filter((r) => participants.includes(r.file)).every((r) => r.kick === PRODUCT_KICK));
});

test('the first failed run stops new runs from starting', async () => {
  const started = [];
  const run = async (args) => {
    started.push(args);
    if (args === 'bad') throw new Error('crashed');
    return [args];
  };
  // Two at once: the run already started beside the failure finishes, and nothing after it starts.
  await assert.rejects(runAll(['bad', 'a', 'b', 'c', 'd'], 2, run), /crashed/);
  assert.deepEqual(started, ['bad', 'a']);
  assert.deepEqual(await runAll(['a', 'b', 'c'], 2, run), ['a', 'b', 'c']);
});
