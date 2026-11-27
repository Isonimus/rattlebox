// Tests for the pure half of scripts/build-envs.mjs: which envs the gate builds, and which it
// refuses to guess at.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { BUILT_ELSEWHERE, INI, buildableEnvs, isTestEnv, parseEnvs } from './build-envs.mjs';

// platformio.ini in miniature: a firmware env, a test env, one extending it, a probe, and a
// multi-line value whose continuation lines must not read as keys.
const SAMPLE = `
[platformio]
default_envs = cardputer

[env:cardputer]
platform = espressif32
build_src_filter = +<*> -<hal/boards/*>
    +<hal/boards/cardputer/>
    test_framework = this is a continuation line, not a key

[env:native]
platform = native
test_framework = unity

[env:native-fairness]
extends = env:native
build_flags = -D FAIRNESS_THREE_DICE

; A probe: a host program with a main of its own, so pio run links it.
[env:native-clack-probe]
platform = native
build_src_filter = +<probes/clack/>
`;

test('every env is found, in the order it is declared', () => {
  assert.deepEqual([...parseEnvs(SAMPLE).keys()], ['cardputer', 'native', 'native-fairness', 'native-clack-probe']);
});

test('a continuation line is not read as a key', () => {
  // The indented `test_framework = ...` belongs to build_src_filter's value. Reading it as a key
  // would mark the firmware env test-only and drop it out of every build.
  assert.equal(isTestEnv(parseEnvs(SAMPLE), 'cardputer'), false);
});

test('a test env is one that declares test_framework or extends one that does', () => {
  const envs = parseEnvs(SAMPLE);
  assert.equal(isTestEnv(envs, 'native'), true);
  assert.equal(isTestEnv(envs, 'native-fairness'), true);
  assert.equal(isTestEnv(envs, 'native-clack-probe'), false);
});

test('the gate builds the probes and leaves the test envs and the firmware alone', () => {
  assert.deepEqual(buildableEnvs(SAMPLE, ['cardputer']), ['native-clack-probe']);
});

test('an extends that names nothing, or a cycle, is a failure rather than a guess', () => {
  const missing = parseEnvs('[env:a]\nextends = env:nowhere\n');
  assert.throws(() => isTestEnv(missing, 'a'), /extends env:nowhere, which is not declared/);
  const cycle = parseEnvs('[env:a]\nextends = env:b\n\n[env:b]\nextends = env:a\n');
  assert.throws(() => isTestEnv(cycle, 'a'), /extend each other in a cycle/);
});

test('an env listed as built elsewhere must exist', () => {
  // Otherwise a rename drops that env out of CI with nothing to say so.
  assert.throws(() => buildableEnvs(SAMPLE, ['cardputer', 'gone']), /declares no env:gone/);
});

test('the real platformio.ini classifies every env, and the firmware envs are not among them', () => {
  const names = buildableEnvs(readFileSync(INI, 'utf8'));
  assert.ok(names.length > 0);
  for (const name of BUILT_ELSEWHERE) assert.ok(!names.includes(name), `${name} is built by the firmware job`);
  // The two test envs link only under `pio test`, so a gate that ran `pio run` on them would fail
  // on a healthy tree.
  for (const name of ['native', 'native-fairness']) assert.ok(!names.includes(name), `${name} is a test env`);
  assert.ok(names.includes('m5stickc-plus2-render-probe'));
});
