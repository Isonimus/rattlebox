#!/usr/bin/env node
// Builds every PlatformIO env that is not a test env and not already built elsewhere in CI.
//
// Slice 0035 changed `viewDie`'s signature and added a call into the die catalog, and broke three
// envs that nothing builds: both render probes and the clack and level probes. Nobody noticed for
// a commit, because CI builds the two firmware envs and runs the two test envs, and the probes sit
// between the two. A probe that no longer compiles is a measurement that cannot be repeated, which
// is the one thing a probe is for.
//
// The env list is read from platformio.ini rather than repeated here, because a repeated list is
// the bug this script exists to catch. A test env is recognised by `test_framework`, its own or
// one it extends, and is skipped: `pio run` on it fails to link by design, since a test env has no
// main() of its own.
//
// Usage: node scripts/build-envs.mjs [--list]
//
// Everything above main() is pure and tested by scripts/build-envs.test.mjs.

import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

export const INI = 'platformio.ini';

// Built by the `firmware` job, which also gates their warnings and checks their release images.
// Building them again here would add minutes and prove nothing new. The script fails if either
// name stops existing, so this list cannot quietly skip an env that was renamed.
export const BUILT_ELSEWHERE = ['cardputer', 'm5stickc-plus2'];

const SECTION = /^\[([^\]]+)\]\s*$/;
// A key at column zero. An indented line continues the value above it and is never a key.
const KEY = /^([A-Za-z_][\w-]*)\s*=\s*(.*)$/;

/**
 * The envs in @p text as `Map<name, {extendsEnv: string|null, testFramework: boolean}>`, in the
 * order they are declared.
 */
export function parseEnvs(text) {
  const envs = new Map();
  let current = null;
  for (const raw of text.split('\n')) {
    const line = raw.replace(/\s+$/, '');
    if (line.length === 0 || line.startsWith(';')) continue;
    const section = SECTION.exec(line);
    if (section) {
      current = section[1].startsWith('env:') ? section[1].slice('env:'.length) : null;
      if (current !== null) envs.set(current, { extendsEnv: null, testFramework: false });
      continue;
    }
    if (current === null) continue;
    const key = KEY.exec(line);
    if (key === null) continue;
    if (key[1] === 'extends') {
      const target = key[2].trim();
      // Only env sections matter here; `extends = common` would name something else entirely.
      envs.get(current).extendsEnv = target.startsWith('env:') ? target.slice('env:'.length) : null;
    }
    if (key[1] === 'test_framework') envs.get(current).testFramework = true;
  }
  return envs;
}

/**
 * Whether @p name is a test env: it declares `test_framework`, or extends one that does.
 *
 * Throws on an `extends` that names no env and on a cycle, rather than reporting "not a test env"
 * for a file it failed to understand and then building something that cannot link.
 */
export function isTestEnv(envs, name) {
  const seen = new Set();
  let at = name;
  while (at !== null) {
    if (seen.has(at)) throw new Error(`${INI}: the envs ${[...seen].join(', ')} extend each other in a cycle`);
    seen.add(at);
    const env = envs.get(at);
    if (env === undefined) throw new Error(`${INI}: env ${name} extends env:${at}, which is not declared`);
    if (env.testFramework) return true;
    at = env.extendsEnv;
  }
  return false;
}

/** The envs this script builds: every declared env that is neither a test env nor built elsewhere. */
export function buildableEnvs(text, builtElsewhere = BUILT_ELSEWHERE) {
  const envs = parseEnvs(text);
  for (const name of builtElsewhere) {
    // A renamed env would otherwise drop out of CI silently, which is this script's own failure mode.
    if (!envs.has(name)) throw new Error(`${INI} declares no env:${name}, but it is listed as built elsewhere`);
  }
  return [...envs.keys()].filter((name) => !builtElsewhere.includes(name) && !isTestEnv(envs, name));
}

function main() {
  const names = buildableEnvs(readFileSync(INI, 'utf8'));
  if (process.argv.includes('--list')) {
    console.log(names.join('\n'));
    return;
  }
  console.log(`Building ${names.length} env(s) that no other job builds: ${names.join(', ')}`);
  const failed = [];
  for (const name of names) {
    process.stdout.write(`  ${name}... `);
    const build = spawnSync('pio', ['run', '-e', name], { encoding: 'utf8' });
    if (build.status === 0) {
      console.log('ok');
      continue;
    }
    console.log('FAILED');
    failed.push(name);
    // The whole log, not a guessed line: a link failure names symbols and a compile failure names
    // a file, and which one it is decides the fix.
    console.error(`${build.stdout}${build.stderr}`);
  }
  if (failed.length > 0) {
    console.error(`FAILED: ${failed.join(', ')}`);
    process.exit(1);
  }
  console.log(`PASSED: ${names.length} env(s) build`);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) main();
