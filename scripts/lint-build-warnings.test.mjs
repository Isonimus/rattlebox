import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';

import { scanLog } from './lint-build-warnings.mjs';

const GATE = new URL('./lint-build-warnings.mjs', import.meta.url).pathname;

/** The gate's exit code on a log holding @p text. */
function gateExit(text) {
  const log = join(mkdtempSync(join(tmpdir(), 'warnings-')), 'build.log');
  writeFileSync(log, text);
  try {
    execFileSync('node', [GATE, log], { stdio: 'pipe' });
    return 0;
  } catch (error) {
    return error.status;
  }
}

test('a warning in our src/ is found, with its place and message', () => {
  const log = 'src/physics/die_result.cpp:22:31: warning: unused variable \'x\' [-Wunused-variable]\n';
  assert.deepEqual(scanLog(log), [
    { path: 'src/physics/die_result.cpp', line: 22, message: "unused variable 'x' [-Wunused-variable]" },
  ]);
});

test("a library's or the framework's warnings are not ours", () => {
  const log = [
    ".pio/libdeps/cardputer/LovyanGFX/src/lgfx/v1/panel/Panel_RA8875.hpp:63:25: warning: unused parameter 'invert' [-Wunused-parameter]",
    '.platformio/packages/framework-arduinoespressif32/cores/esp32/Arduino.h:1:1: warning: something',
  ].join('\n');
  assert.deepEqual(scanLog(log), []);
});

test('notes, include context, errors and a ./ prefix are read correctly', () => {
  const log = [
    'In file included from src/main.cpp:4:',
    "src/main.cpp:9:5: note: in expansion of macro 'X'",
    'src/main.cpp:10:1: error: expected declaration',
    './src/render/band_drawing.cpp:7: warning: comparison of integer expressions [-Wsign-compare]',
  ].join('\n');
  assert.deepEqual(scanLog(log), [
    { path: 'src/render/band_drawing.cpp', line: 7, message: 'comparison of integer expressions [-Wsign-compare]' },
  ]);
});

test('the gate fails on our warning and passes on a clean log', () => {
  assert.equal(gateExit('src/main.cpp:1:1: warning: w\n'), 1);
  assert.equal(gateExit('Compiling .pio/build/cardputer/src/main.cpp.o\n'), 0);
});

test('the gate fails on an empty or missing log rather than pass it', () => {
  assert.equal(gateExit(''), 1);
  try {
    execFileSync('node', [GATE, join(tmpdir(), 'no-such-build.log')], { stdio: 'pipe' });
    assert.fail('passed a log that does not exist');
  } catch (error) {
    assert.equal(error.status, 1);
  }
});
