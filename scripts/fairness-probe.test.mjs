// Tests for the pure half of scripts/fairness-probe.mjs (slice 0008), on lines in the form the
// probe program prints them.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  TALLY_SEEDS, TALLY_SIZES, TRACES, checkRuns, chiSquarePValue, chiSquareStatistic, parseProbeLine,
  summarizeTally, summarizeTraces, summaryMarkdown, tiltGap,
} from './fairness-probe.mjs';

const GRID = '[trace-grid] linear=1,5 angular=0.1,0.5';

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

/** A complete, passing run: every trace and every tally the probe runs. */
function completeRun() {
  const lines = [GRID];
  for (const t of TRACES) for (let i = 0; i < t.rolls; i++) lines.push(trace(t.die, '0.0000'));
  for (const size of TALLY_SIZES) {
    for (const seed of TALLY_SEEDS) lines.push(tally(size.die, size.dice, seed, size.die === 'D20' ? FAIR_D20 : FAIR_D6));
  }
  return lines.map(parseProbeLine);
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
  assert.deepEqual(parseProbeLine(GRID), { kind: 'grid', linear: [1, 5], angular: [0.1, 0.5] });
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
