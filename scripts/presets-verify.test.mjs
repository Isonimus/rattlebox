// The pure half of scripts/presets-verify.mjs: what it reads off the board's lines and what it
// makes of them. The device half needs a board on a port, so CI runs this (ADR-0003).

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { checkRun, parseArgs, parsePresetLine, summaryMarkdown, STEPPED, TYPED } from './presets-verify.mjs';

/** The lines a boot prints, with @p storeLine where the store reports: just before "ready". */
const BOOT_WITH = (storeLine) => [...BOOT_STEPS, storeLine, '[boot] M5StickC Plus2 ready'];

/** Every boot line up to the store's own, so a case can get to the part it is about. */
const BOOT_STEPS = [
  '[boot] Rattlebox v0.1.0 on M5StickC Plus2',
  '[boot] power OK: HOLD latch on G4',
  '[boot] display OK: ST7789 240x135',
  '[boot] input OK: two buttons on G37 and G39',
  '[boot] imu OK: MPU6886 0x68 accel +-16 g gyro +-2000 dps at 100 Hz',
  '[boot] rng OK: esp_random with the SAR ADC as entropy',
  '[boot] storage OK: namespace rattlebox, key presets',
];

test('a restore line is read as the choice it names', () => {
  assert.deepEqual(parsePresetLine('[presets] restored D6 x3 mod=+2, 1 saved'), {
    kind: 'presets-restored',
    type: 'D6',
    count: 3,
    modifier: 2,
    saved: 1,
  });
  assert.deepEqual(parsePresetLine('[presets] restored D4 x1 mod=-9, 4 saved'), {
    kind: 'presets-restored',
    type: 'D4',
    count: 1,
    modifier: -9,
    saved: 4,
  });
});

test('an empty store and a refused record are told apart', () => {
  assert.equal(parsePresetLine('[presets] nothing stored, starting at D20 x1 mod=+0').kind, 'presets-nothing');
  assert.equal(parsePresetLine('[presets] record refused, starting at D20 x1 mod=+0').kind, 'presets-record-refused');
});

test('a read or write failure is read as one, with its reason', () => {
  assert.deepEqual(parsePresetLine('[presets] read failed: ESP_ERR_NVS_INVALID_HANDLE, starting at D20 x1 mod=+0'), {
    kind: 'presets-read-failed',
    reason: 'ESP_ERR_NVS_INVALID_HANDLE',
  });
  assert.deepEqual(parsePresetLine('[presets] write failed: ESP_ERR_NVS_NOT_ENOUGH_SPACE'), {
    kind: 'presets-write-failed',
    reason: 'ESP_ERR_NVS_NOT_ENOUGH_SPACE',
  });
});

test('a save and a recall are read with their slot', () => {
  assert.deepEqual(parsePresetLine('[presets] saved 2: D6 x3 mod=+2'), {
    kind: 'presets-saved',
    slot: 2,
    type: 'D6',
    count: 3,
    modifier: 2,
  });
  assert.deepEqual(parsePresetLine('[presets] recalled 2: D6 x3 mod=+2'), {
    kind: 'presets-recalled',
    slot: 2,
    type: 'D6',
    count: 3,
    modifier: 2,
  });
  assert.equal(parsePresetLine('[presets] recall: nothing saved').kind, 'presets-none-saved');
});

test('the roll and notation lines still parse, so one log is read', () => {
  assert.equal(parsePresetLine('[roll] launch D6 x3').kind, 'launch');
  assert.equal(parsePresetLine('[roll] typed 3d6+2 accepted D6 x3 mod=+2').kind, 'typed-accepted');
});

/** A whole passing run: the lines a board prints and the plan the script built alongside them. */
function passingRun() {
  const typedLaunch = { kind: 'launch', choice: { type: TYPED.choice.type, count: TYPED.choice.count } };
  const steppedLaunch = { kind: 'launch', choice: { type: STEPPED.choice.type, count: STEPPED.choice.count } };
  const lines = [
    // The store's line, then the ready line, then the roll setup() throws on every boot.
    ...BOOT_WITH('[presets] nothing stored, starting at D20 x1 mod=+0'),
    '[roll] launch D20 x1',
    '[roll] typed 3d6+2 accepted D6 x3 mod=+2',
    '[roll] launch D6 x3',
    ...BOOT_WITH('[presets] restored D6 x3 mod=+2, 0 saved'),
    '[roll] launch D6 x3',
    '[presets] saved 1: D6 x3 mod=+2',
    '[roll] launch D8 x3',
    ...BOOT_WITH('[presets] restored D8 x3 mod=+2, 1 saved'),
    '[roll] launch D8 x3',
    '[presets] recalled 1: D6 x3 mod=+2',
    '[roll] launch D6 x3',
  ];
  const plan = [
    { kind: 'presets-nothing' },
    // The first boot's roll throws whatever the board had stored, which this run cannot predict.
    { kind: 'launch' },
    { kind: 'typed-accepted', choice: TYPED.choice },
    typedLaunch,
    { kind: 'presets-restored', choice: TYPED.choice },
    typedLaunch,
    { kind: 'presets-saved', choice: TYPED.choice, slot: 1 },
    steppedLaunch,
    { kind: 'presets-restored', choice: STEPPED.choice },
    steppedLaunch,
    { kind: 'presets-recalled', choice: TYPED.choice, slot: 1 },
    typedLaunch,
  ];
  return { parsed: lines.map(parsePresetLine), plan };
}

test('a run that did everything it was asked has no failures', () => {
  const { parsed, plan } = passingRun();
  assert.deepEqual(checkRun(parsed, plan), []);
});

// The plan expects one roll per boot, because setup() throws one after every boot. A plan that
// left them out could not pass on any real board, and the fixture that omitted them hid it.
test('the roll every boot throws is expected, not filtered out', () => {
  const { parsed, plan } = passingRun();
  const withoutBootRolls = parsed.filter((entry, at) => {
    const previous = parsed[at - 1];
    return !(entry.kind === 'launch' && previous !== undefined && previous.kind === 'boot-ready');
  });
  // Three boots, so three fewer launches than the plan expects.
  assert.equal(parsed.length - withoutBootRolls.length, 3);
  const failures = checkRun(withoutBootRolls, plan);
  assert.match(failures.join('\n'), /expected 12 reported steps, saw 9/);
});

test('a boot that restored the wrong choice fails', () => {
  const { parsed, plan } = passingRun();
  // The reset came back on the boot default instead of what was stored, which is the whole claim.
  const at = parsed.findIndex((entry) => entry.kind === 'presets-restored');
  parsed[at] = parsePresetLine('[presets] restored D20 x1 mod=+0, 0 saved');
  const failures = checkRun(parsed, plan);
  assert.equal(failures.length, 1);
  assert.match(failures[0], /presets-restored\): 1 D20 mod \+0, not 3 D6 mod \+2/);
});

test('a record the board refused fails the run wherever it appears', () => {
  const { parsed, plan } = passingRun();
  const at = parsed.findIndex((entry) => entry.kind === 'presets-restored');
  parsed[at] = parsePresetLine('[presets] record refused, starting at D20 x1 mod=+0');
  plan[4] = { kind: 'presets-record-refused' };
  // The plan now matches, so the only thing that can fail the run is the refusal itself: a record
  // this run wrote and could not read back is a failure however the plan reads.
  const failures = checkRun(parsed, plan);
  assert.equal(failures.length, 1);
  assert.match(failures[0], /the stored record was refused/);
});

test('a store that failed to write fails the run', () => {
  const { parsed, plan } = passingRun();
  parsed.push(parsePresetLine('[presets] write failed: ESP_ERR_NVS_NOT_ENOUGH_SPACE'));
  const failures = checkRun(parsed, plan);
  assert.match(failures.join('\n'), /failed to write: ESP_ERR_NVS_NOT_ENOUGH_SPACE/);
});

test('a recall that returned another slot fails', () => {
  const { parsed, plan } = passingRun();
  const at = parsed.findIndex((entry) => entry.kind === 'presets-recalled');
  parsed[at] = parsePresetLine('[presets] recalled 3: D6 x3 mod=+2');
  const failures = checkRun(parsed, plan);
  assert.equal(failures.length, 1);
  assert.match(failures[0], /slot 3, not slot 1/);
});

test('a recall that threw nothing fails', () => {
  const { parsed, plan } = passingRun();
  // Drop the launch the recall should have thrown: the count no longer matches the plan, and the
  // missing step is named.
  parsed.pop();
  const failures = checkRun(parsed, plan);
  assert.match(failures.join('\n'), /expected 12 reported steps, saw 11/);
  assert.match(failures.join('\n'), /step 12: expected launch, and the board reported nothing/);
});

test('a recall that found nothing after a save fails', () => {
  const { parsed, plan } = passingRun();
  const at = parsed.findIndex((entry) => entry.kind === 'presets-recalled');
  parsed[at] = parsePresetLine('[presets] recall: nothing saved');
  const failures = checkRun(parsed, plan);
  assert.match(failures.join('\n'), /step 11: expected presets-recalled, the board reported presets-none-saved/);
});

test('a panic anywhere in the log fails the run', () => {
  const { parsed, plan } = passingRun();
  parsed.push(parsePresetLine('Guru Meditation Error: Core 1 panic\'ed (LoadProhibited)'));
  assert.ok(checkRun(parsed, plan).length > 0);
});

test('parseArgs insists on a known env and a port', () => {
  assert.deepEqual(parseArgs(['--env', 'cardputer', '--port', '/dev/ttyACM0']), {
    env: 'cardputer',
    port: '/dev/ttyACM0',
  });
  assert.throws(() => parseArgs(['--env', 'nonesuch', '--port', '/dev/ttyACM0']), /--env must be one of/);
  assert.throws(() => parseArgs(['--env', 'cardputer']), /--port is required/);
  assert.throws(() => parseArgs(['--nonesuch']), /unknown argument/);
});

test('the summary names each step and what the board said', () => {
  const markdown = summaryMarkdown('m5stickc-plus2', [
    { what: 'reset, then boot', line: 'restored 3 D6 mod +2, 1 saved' },
    { what: 'held the primary control', line: 'recalled 1: 3 D6 mod +2' },
  ]);
  assert.match(markdown, /# Presets and the remembered roll on the m5stickc-plus2/);
  assert.match(markdown, /\| reset, then boot \| restored 3 D6 mod \+2, 1 saved \|/);
  assert.match(markdown, /\| held the primary control \| recalled 1: 3 D6 mod \+2 \|/);
});
