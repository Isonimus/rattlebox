import assert from 'node:assert/strict';
import test from 'node:test';

import { CASES, checkRun, parseArgs, parseNotationLine, summaryMarkdown } from './notation-verify.mjs';

// The parsing half of the dice notation device check (slice 0037), which runs with no board: what
// the firmware's lines mean, and which runs the check calls a failure.

const BOOT_LINES = [
  '[boot] power OK: HOLD latch on G4',
  '[boot] display OK: ST7789 240x135',
  '[boot] input OK: TCA8418 keyboard at 0x34',
  '[boot] imu OK: BMI270 0x68 accel +-16 g gyro +-2000 dps at 100 Hz',
  '[boot] rng OK: esp_random with the SAR ADC as entropy',
  '[boot] Cardputer ADV ready',
];

const accepted = (expression, type, count, modifier) =>
  `[roll] typed ${expression} accepted ${type} x${count} mod=${modifier >= 0 ? '+' : '-'}${Math.abs(modifier)}`;
const launch = (type, count) => `[roll] launch ${type} x${count}`;

/** The lines a board prints for a run that does everything CASES asks of it. */
function passingLines() {
  const lines = [...BOOT_LINES];
  for (const expected of CASES) {
    if (expected.accepted) {
      lines.push(accepted(expected.expression, expected.type, expected.count, expected.modifier));
      lines.push(launch(expected.type, expected.count));
    } else {
      lines.push(`[roll] typed ${expected.expression} refused: the tray holds 3`);
    }
  }
  return lines;
}

const checkLines = (lines) => checkRun(lines.map(parseNotationLine));

test('an accepted expression parses into the choice the board read', () => {
  assert.deepEqual(parseNotationLine('[roll] typed 3d6+2 accepted D6 x3 mod=+2'), {
    kind: 'typed-accepted',
    expression: '3d6+2',
    type: 'D6',
    count: 3,
    modifier: 2,
  });
  assert.equal(parseNotationLine('[roll] typed 1d20-9 accepted D20 x1 mod=-9').modifier, -9);
});

test('a refusal parses with the reason the band shows, spaces and all', () => {
  assert.deepEqual(parseNotationLine('[roll] typed 4d6 refused: the tray holds 3'), {
    kind: 'typed-refused',
    expression: '4d6',
    reason: 'the tray holds 3',
  });
});

test('a full field parses, and an empty one still reports its text', () => {
  assert.deepEqual(parseNotationLine('[roll] typed full: 1111111111'), { kind: 'typed-full', text: '1111111111' });
  assert.deepEqual(parseNotationLine('[roll] typed full: '), { kind: 'typed-full', text: '' });
});

test('the roll lines the product already printed still parse', () => {
  assert.deepEqual(parseNotationLine('[roll] launch D6 x3'), { kind: 'launch', type: 'D6', count: 3 });
  assert.equal(parseNotationLine('[boot] Cardputer ADV ready').kind, 'boot-ready');
  assert.equal(parseNotationLine('nothing the check knows').kind, 'other');
});

test('a run that reads every expression as it reads and throws only the accepted ones passes', () => {
  assert.deepEqual(checkLines(passingLines()), []);
});

// ADR-0036: an expression the tray cannot throw is refused, never reduced to one it can.
test('a refusal that throws a roll anyway fails', () => {
  const refused = CASES.find((expected) => !expected.accepted);
  const lines = passingLines();
  lines.splice(lines.indexOf(`[roll] typed ${refused.expression} refused: the tray holds 3`) + 1, 0, launch('D6', 3));
  const failures = checkLines(lines);
  assert.ok(failures.some((line) => line === `${refused.expression}: refused and then threw 3 D6 anyway`), failures.join('; '));
});

test('an expression refused that the tray can throw fails, and one accepted that it cannot', () => {
  const good = CASES.find((expected) => expected.accepted);
  const bad = CASES.find((expected) => !expected.accepted);
  const refusedGood = passingLines().map((line) =>
    line.startsWith(`[roll] typed ${good.expression} accepted`) ? `[roll] typed ${good.expression} refused: no die that size` : line,
  );
  assert.ok(checkLines(refusedGood).some((line) => line === `${good.expression}: refused with "no die that size"`));

  const acceptedBad = passingLines().map((line) =>
    line.startsWith(`[roll] typed ${bad.expression} refused`) ? accepted(bad.expression, 'D6', 3, 0) : line,
  );
  const failures = checkLines(acceptedBad);
  assert.ok(failures.some((line) => line === `${bad.expression}: accepted, and it asks for more than the tray holds`), failures.join('; '));
});

test('an accepted expression read as another roll fails', () => {
  const good = CASES.find((expected) => expected.accepted);
  const lines = passingLines().map((line) =>
    line.startsWith(`[roll] typed ${good.expression} accepted`) ? accepted(good.expression, good.type, good.count, 0) : line,
  );
  const failures = checkLines(lines);
  assert.ok(failures.some((line) => line.includes(`${good.expression}: read as`)), failures.join('; '));
});

test('an accepted expression that threw nothing fails', () => {
  const good = CASES.find((expected) => expected.accepted);
  const lines = passingLines().filter((line) => line !== launch(good.type, good.count));
  assert.ok(checkLines(lines).some((line) => line === `${good.expression}: accepted and threw nothing`));
});

test('a submission the board never answered, a field that filled up and a stray error each fail', () => {
  const missing = checkLines(passingLines().filter((line) => !line.includes(` typed ${CASES[0].expression} `)));
  assert.ok(missing.some((line) => line === `expected ${CASES.length} submissions, saw ${CASES.length - 1}`), missing.join('; '));

  const filled = passingLines().map((line) =>
    line.startsWith(`[roll] typed ${CASES[0].expression} accepted`) ? '[roll] typed full: 3d6+23d6+2' : line,
  );
  assert.ok(checkLines(filled).some((line) => line === `${CASES[0].expression}: the field filled up at "3d6+23d6+2"`));

  const errored = checkLines([...passingLines(), '[roll] FAILED: not every die locked within 20 s']);
  assert.ok(errored.some((line) => /error line: \[roll\] FAILED/.test(line)));
});

test('a boot that never came up fails whatever the expressions did', () => {
  const failures = checkLines(passingLines().filter((line) => !line.includes('display')));
  assert.ok(failures.includes('boot: no "[boot] display OK" line'));
});

test('the summary names every expression and what became of it', () => {
  const markdown = summaryMarkdown('cardputer', passingLines().map(parseNotationLine).filter((entry) => entry.kind.startsWith('typed-')));
  for (const expected of CASES) assert.match(markdown, new RegExp(`\`${expected.expression.replace('+', '\\+')}\``));
  assert.match(markdown, /\| accepted \|/);
  assert.match(markdown, /\| refused \|/);
});

test('the environment and the port are both required', () => {
  assert.deepEqual(parseArgs(['--env', 'cardputer', '--port', '/dev/ttyACM0']), { env: 'cardputer', port: '/dev/ttyACM0' });
  assert.throws(() => parseArgs(['--env', 'cardputer']), /--port is required/);
  assert.throws(() => parseArgs(['--env', 'nothing', '--port', '/dev/ttyACM0']), /--env must be one of/);
  assert.throws(() => parseArgs(['--what']), /unknown argument --what/);
});
