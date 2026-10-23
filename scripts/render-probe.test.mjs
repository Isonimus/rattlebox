// Tests for the pure half of scripts/render-probe.mjs (slice 0006), on log lines in the
// form the probe firmware prints them.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { checkLog, framesPerSecond, parseArgs, parseRenderLine, summarize, summaryMarkdown } from './render-probe.mjs';

const PHASE_FIGURES = 'clear=900/910/950 geometry=300/320/400 raster=700/800/900 numerals=200/250/300 push=13000/13010/13100 frame=15100/15200/15900';

function result(mode, edgePx) {
  return `[render] result ${mode} edge=${edgePx} frames=600 ${PHASE_FIGURES}`;
}

const BOOT = ['[boot] power OK: x', '[boot] display OK: ST7789 240x135', '[boot] StickC render probe ready'];
const ALL_RUNS = [result('blocking', 12), result('blocking', 24), result('overlapped', 12), result('overlapped', 24)];

function parseAll(lines) {
  return lines.map(parseRenderLine);
}

test('a result line parses into its run and every phase', () => {
  const run = parseRenderLine(result('overlapped', 24));
  assert.equal(run.kind, 'result');
  assert.equal(run.mode, 'overlapped');
  assert.equal(run.edgePx, 24);
  assert.equal(run.frames, 600);
  assert.deepEqual(run.phases.push, { medianUs: 13000, p95Us: 13010, maxUs: 13100 });
  assert.deepEqual(Object.keys(run.phases), ['clear', 'geometry', 'raster', 'numerals', 'push', 'frame']);
});

test('a result line missing a phase is refused rather than summarised without it', () => {
  assert.throws(() => parseRenderLine('[render] result blocking edge=12 frames=600 clear=1/2/3'), /without geometry/);
});

test('an unavailable mode and the second canvas parse with their sizes', () => {
  assert.deepEqual(
    parseRenderLine('[render] unavailable overlapped: a second canvas needs 64800 bytes, largest free DMA block 61428'),
    { kind: 'unavailable', mode: 'overlapped', neededBytes: 64800, largestBlockBytes: 61428 },
  );
  assert.deepEqual(parseRenderLine('[render] second canvas: 64800 bytes from a largest free DMA block of 110580'), {
    kind: 'second-canvas', neededBytes: 64800, largestBlockBytes: 110580,
  });
});

test('boot and error lines keep the skeleton check\'s meaning', () => {
  assert.equal(parseRenderLine('[boot] canvas FAILED: no heap for the frame buffer').kind, 'boot-failed');
  assert.equal(parseRenderLine('Guru Meditation Error: Core  1 panic\'ed').kind, 'error');
  assert.equal(parseRenderLine('[render] done').kind, 'done');
});

test('a log with every run and done passes', () => {
  assert.deepEqual(checkLog(parseAll([...BOOT, ...ALL_RUNS, '[render] done'])), []);
});

test('an overlapped mode declared unavailable needs no overlapped runs', () => {
  const lines = [...BOOT, ALL_RUNS[0], ALL_RUNS[1],
    '[render] unavailable overlapped: a second canvas needs 64800 bytes, largest free DMA block 61428', '[render] done'];
  assert.deepEqual(checkLog(parseAll(lines)), []);
});

test('a missing run, a missing done, a failed boot and an error line are each named', () => {
  assert.deepEqual(checkLog(parseAll([...BOOT, ALL_RUNS[0], ALL_RUNS[1], ALL_RUNS[2], '[render] done'])), ['no overlapped run at edge 24 px']);
  assert.deepEqual(checkLog(parseAll([...BOOT, ...ALL_RUNS])), ['the board never printed [render] done']);
  const failedBoot = checkLog(parseAll(['[boot] canvas FAILED: no heap for the frame buffer']));
  assert.equal(failedBoot[0], 'boot: canvas FAILED: no heap for the frame buffer');
  const withError = checkLog(parseAll([...BOOT, '[  305][E][Wire.cpp:520] oops', ...ALL_RUNS, '[render] done']));
  assert.deepEqual(withError, ['board error: [  305][E][Wire.cpp:520] oops']);
});

test('the blocking mode may not excuse itself as unavailable', () => {
  const lines = [...BOOT, '[render] unavailable blocking: a second canvas needs 1 bytes, largest free DMA block 0',
    ALL_RUNS[2], ALL_RUNS[3], '[render] done'];
  assert.deepEqual(checkLog(parseAll(lines)), ['the blocking mode reported itself unavailable']);
});

test('frames per second come from the frame time', () => {
  assert.equal(framesPerSecond(16667), 60);
  assert.equal(framesPerSecond(15200), 65.8);
});

test('the summary carries each run\'s fps and the markdown has one row per run', () => {
  const summary = summarize('m5stickc-plus2-render-probe', parseAll([...BOOT, ...ALL_RUNS, '[render] done']));
  assert.equal(summary.runs.length, 4);
  assert.equal(summary.runs[0].fpsAtMedian, 66.2);
  assert.equal(summary.runs[0].fpsAtP95, 65.8);
  const markdown = summaryMarkdown(summary);
  assert.match(markdown, /\| blocking \| 12 px \| 900 \/ 910 \/ 950 \|/);
  assert.equal(markdown.split('\n').filter((line) => line.startsWith('| overlapped')).length, 2);
});

test('arguments need a render probe env and a port', () => {
  assert.deepEqual(parseArgs(['--env', 'cardputer-render-probe', '--port', 'p']), { env: 'cardputer-render-probe', port: 'p' });
  assert.throws(() => parseArgs(['--env', 'cardputer', '--port', 'p']), /--env must be one of/);
  assert.throws(() => parseArgs(['--env', 'cardputer-render-probe']), /--port is required/);
});
