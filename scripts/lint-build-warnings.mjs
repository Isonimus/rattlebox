// Gate: no compiler warnings in our own src/ (ADR-0022). Adapted from the Adversary's gate
// (adversary:ADR-0083), by the same author.
//
// Usage: node scripts/lint-build-warnings.mjs build.log
//
// Scans a captured PlatformIO build log and fails on any `warning:` line whose path is under src/.
// Scoped by path rather than -Werror: LovyanGFX's headers warn under the -Wextra our sources build
// with (platformio.ini), and those warnings print against the library's path, not ours, so the
// gate never attributes them to us and never has to silence them. CI builds cold, so every one of
// our translation units is in its log; locally an incremental build shows only what recompiled.

import { existsSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

// "<path>:<line>[:<col>]: warning: <message>". The path stops at the first ":<digits>" so a colon
// in the message is not taken for one. Notes, "In file included from" lines and errors do not match.
const WARNING_LINE = /^(.+?):(\d+)(?::(\d+))?: warning: (.*)$/;

/** One finding per warning on a path under src/. */
export function scanLog(text) {
  const findings = [];
  for (const line of text.split('\n')) {
    const match = WARNING_LINE.exec(line);
    if (!match) continue;
    const path = match[1].replace(/^\.\//, '');
    if (!path.startsWith('src/')) continue;
    findings.push({ path, line: Number(match[2]), message: match[4] });
  }
  return findings;
}

function main(logPath) {
  // A log that was never written would otherwise read as a build without warnings.
  if (!logPath || !existsSync(logPath) || statSync(logPath).size === 0) {
    console.error(`lint-build-warnings: no build log at ${logPath}; capture one with \`pio run -e <env> 2>&1 | tee build.log\``);
    return 1;
  }
  const findings = scanLog(readFileSync(logPath, 'utf8'));
  for (const { path, line, message } of findings) console.error(`${path}:${line}: warning: ${message}`);
  if (findings.length > 0) {
    console.error(`\nlint-build-warnings: ${findings.length} warning(s) in src/, which builds warning-free (ADR-0022)`);
    return 1;
  }
  console.log(`lint-build-warnings: no warnings in src/ (${logPath})`);
  return 0;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) process.exit(main(process.argv[2]));
