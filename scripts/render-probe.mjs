#!/usr/bin/env node
// Render probe (slice 0006): what a frame of three flat-shaded D20s costs on each board,
// and whether drawing can overlap the panel push.
//
// Resets a board running the probe firmware through scripts/serial_reset_monitor.py, reads
// the runs it prints at boot, then takes a screen capture of the last frame. Fails on any
// boot failure, error line or panic, or a run that never reported. Writes
// artifacts/render-probe/<env>/: summary.json, summary.md and the PNG.
//
// Usage: node scripts/render-probe.mjs --env cardputer-render-probe --port /dev/ttyACM0
// Flash the probe first: pio run -e <env> -t upload
//
// Everything above main() is pure and tested by scripts/render-probe.test.mjs.

import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { createInterface } from 'node:readline';
import { pathToFileURL } from 'node:url';

import { parseLine } from './skeleton-verify.mjs';

export const ENVS = ['cardputer-render-probe', 'm5stickc-plus2-render-probe'];
// What the firmware runs (src/probes/render/render_probe_main.cpp): the host checks every
// run reported, so a firmware that stops early cannot pass as a shorter probe.
export const MODES = ['blocking', 'overlapped'];
export const EDGES_PX = [12, 24];
export const PHASES = ['clear', 'geometry', 'raster', 'numerals', 'push', 'frame'];

const US_PER_S = 1e6;
const RESULT = /^\[render\] result (\w+) edge=(\d+) frames=(\d+)((?: \w+=\d+\/\d+\/\d+)+)$/;
const UNAVAILABLE = /^\[render\] unavailable (\w+): a second canvas needs (\d+) bytes, largest free DMA block (\d+)$/;
const SECOND_CANVAS = /^\[render\] second canvas: (\d+) bytes from a largest free DMA block of (\d+)$/;
const DONE = /^\[render\] done$/;

/** @returns {{kind: string, [key: string]: unknown}} */
export function parseRenderLine(line) {
  let match;
  if ((match = RESULT.exec(line))) {
    const phases = {};
    for (const field of match[4].trim().split(' ')) {
      const [name, figures] = field.split('=');
      const [medianUs, p95Us, maxUs] = figures.split('/').map(Number);
      phases[name] = { medianUs, p95Us, maxUs };
    }
    const missing = PHASES.filter((phase) => !(phase in phases));
    if (missing.length > 0) throw new Error(`render result without ${missing.join(', ')}: ${line}`);
    return { kind: 'result', mode: match[1], edgePx: Number(match[2]), frames: Number(match[3]), phases };
  }
  if ((match = UNAVAILABLE.exec(line))) {
    return { kind: 'unavailable', mode: match[1], neededBytes: Number(match[2]), largestBlockBytes: Number(match[3]) };
  }
  if ((match = SECOND_CANVAS.exec(line))) {
    return { kind: 'second-canvas', neededBytes: Number(match[1]), largestBlockBytes: Number(match[2]) };
  }
  if (DONE.test(line)) return { kind: 'done' };
  return parseLine(line);
}

/**
 * Every failure in a board's log: a failed boot, an error line, a missing `done`, or a run
 * neither reported nor declared unavailable. Only the overlapped mode may be unavailable:
 * the blocking mode needs nothing the boot did not already allocate.
 */
export function checkLog(parsed) {
  const failures = [];
  for (const entry of parsed) {
    if (entry.kind === 'boot-failed') failures.push(`boot: ${entry.subsystem} FAILED: ${entry.reason}`);
    if (entry.kind === 'error') failures.push(`board error: ${entry.line}`);
  }
  if (!parsed.some((entry) => entry.kind === 'done')) failures.push('the board never printed [render] done');
  const unavailable = new Set(parsed.filter((entry) => entry.kind === 'unavailable').map((entry) => entry.mode));
  if (unavailable.has('blocking')) failures.push('the blocking mode reported itself unavailable');
  for (const mode of MODES) {
    if (unavailable.has(mode)) continue;
    for (const edgePx of EDGES_PX) {
      if (!parsed.some((entry) => entry.kind === 'result' && entry.mode === mode && entry.edgePx === edgePx)) {
        failures.push(`no ${mode} run at edge ${edgePx} px`);
      }
    }
  }
  return failures;
}

/** Frames per second at a frame time, to one decimal. */
export function framesPerSecond(frameUs) {
  return Math.round((US_PER_S / frameUs) * 10) / 10;
}

export function summarize(env, parsed) {
  const runs = parsed.filter((entry) => entry.kind === 'result');
  return {
    env,
    runs: runs.map((run) => ({
      ...run,
      fpsAtMedian: framesPerSecond(run.phases.frame.medianUs),
      fpsAtP95: framesPerSecond(run.phases.frame.p95Us),
    })),
    unavailable: parsed.filter((entry) => entry.kind === 'unavailable'),
    secondCanvas: parsed.find((entry) => entry.kind === 'second-canvas') ?? null,
  };
}

export function summaryMarkdown(summary) {
  const lines = [`# Render probe: ${summary.env}`, '', 'Microseconds per frame, as median / 95th percentile / maximum.', ''];
  lines.push(`| Mode | Edge | ${PHASES.join(' | ')} | fps (median, p95) |`);
  lines.push(`|---|---|${PHASES.map(() => '---').join('|')}|---|`);
  for (const run of summary.runs) {
    const cells = PHASES.map((phase) => {
      const { medianUs, p95Us, maxUs } = run.phases[phase];
      return `${medianUs} / ${p95Us} / ${maxUs}`;
    });
    lines.push(`| ${run.mode} | ${run.edgePx} px | ${cells.join(' | ')} | ${run.fpsAtMedian}, ${run.fpsAtP95} |`);
  }
  for (const entry of summary.unavailable) {
    lines.push('', `**${entry.mode} unavailable**: a second canvas needs ${entry.neededBytes} bytes; the largest free DMA block was ${entry.largestBlockBytes}.`);
  }
  if (summary.secondCanvas) {
    lines.push('', `Second canvas: ${summary.secondCanvas.neededBytes} bytes from a largest free DMA block of ${summary.secondCanvas.largestBlockBytes}.`);
  }
  return `${lines.join('\n')}\n`;
}

export function parseArgs(argv) {
  const args = { env: null, port: null };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--env') args.env = argv[++i] ?? null;
    else if (argv[i] === '--port') args.port = argv[++i] ?? null;
    else throw new Error(`unknown argument ${argv[i]}`);
  }
  if (!ENVS.includes(args.env)) throw new Error(`--env must be one of ${ENVS.join(', ')}`);
  if (!args.port) throw new Error('--port is required, e.g. --port /dev/ttyACM0');
  return args;
}

// ---------------------------------------------------------------- process handling

// Four runs of 600 frames at up to about 25 ms each take about a minute; twice that
// leaves room for a slower board without letting a hung one hold the port for good.
const RUN_TIMEOUT_MS = 120000;

/** Resets the board and collects its parsed lines until `[render] done`, a failed boot, or the timeout. */
function readBoardLog(port) {
  return new Promise((resolve, reject) => {
    const parsed = [];
    const monitor = spawn('python3', ['scripts/serial_reset_monitor.py', '--port', port], { stdio: ['ignore', 'pipe', 'inherit'] });
    const finish = (settle) => {
      clearTimeout(timer);
      monitor.once('exit', settle);
      monitor.kill('SIGTERM');
    };
    const timer = setTimeout(() => finish(() => resolve(parsed)), RUN_TIMEOUT_MS);
    monitor.on('error', (error) => {
      clearTimeout(timer);
      reject(new Error(`could not start the serial monitor: ${error.message}`));
    });
    monitor.on('exit', (code, signal) => {
      if (signal !== 'SIGTERM') {
        clearTimeout(timer);
        reject(new Error(`serial monitor exited early (code ${code}, signal ${signal})`));
      }
    });
    createInterface({ input: monitor.stdout }).on('line', (line) => {
      let entry;
      try {
        entry = parseRenderLine(line);
      } catch (error) {
        finish(() => reject(error));
        return;
      }
      parsed.push(entry);
      if (entry.kind === 'result' || entry.kind === 'unavailable') console.log(`  ${line}`);
      if (entry.kind === 'error') console.error(`  board: ${line}`);
      if (entry.kind === 'done' || entry.kind === 'boot-failed') finish(() => resolve(parsed));
    });
  });
}

async function main() {
  const { env, port } = parseArgs(process.argv.slice(2));
  const outDir = join('artifacts', 'render-probe', env);
  mkdirSync(outDir, { recursive: true });

  console.log(`Resetting the ${env} on ${port}; the runs take about a minute...`);
  const parsed = await readBoardLog(port);
  const failures = checkLog(parsed);

  // The capture opens the port without resetting the board, which holds the last frame.
  if (failures.length === 0) {
    const capture = spawnSync('python3', ['scripts/capture_screen.py', '--port', port, '--out', outDir], { encoding: 'utf8' });
    if (capture.status !== 0) failures.push(`screen capture: ${capture.stderr.trim()}`);
  }

  const summary = summarize(env, parsed);
  writeFileSync(join(outDir, 'summary.json'), `${JSON.stringify({ ...summary, failures }, null, 2)}\n`);
  writeFileSync(join(outDir, 'summary.md'), summaryMarkdown(summary));
  console.log(`\nArtifacts in ${outDir}`);
  if (failures.length > 0) {
    console.error(`FAILED:\n  ${failures.join('\n  ')}`);
    process.exit(1);
  }
  console.log('PASSED: every run reported');
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  });
}
