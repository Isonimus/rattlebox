// The host scripts' die tables against the catalog the firmware compiles from (ADR-0034).
//
// `scripts/fairness-probe.mjs` and `scripts/roll-verify.mjs` each spell out the die names and
// their shapes, because neither may depend on a compiler to do its job: the device check drives a
// board over a serial port. A spelled-out table drifts in silence, so this file builds the
// fairness probe, asks it for the catalog it compiled, and fails on any disagreement.
//
// It is one file rather than a test in each script's own test file on purpose. `node --test` runs
// test files in parallel child processes, so two files calling `readCatalog` would run two
// `pio run -e native-fairness-probe` builds into one build directory at the same time, and one
// could relink `program` while the other was spawning it. Measured: two test files of 1.5 s each
// start 10 ms apart and finish in 1.57 s together. Building once, here, removes the race.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  BODIES_PER_DIE,
  BODY_FACES,
  CHAIN_SIZES,
  FACES,
  TALLY_SIZES,
  bodyCountFailures,
  readCatalog,
  tableFailures,
} from './fairness-probe.mjs';
import { MAX_DICE, ROLLS, TYPE_ORDER, maxDiceOf } from './roll-verify.mjs';

// One build (1.4 s cold) for every check below.
const catalog = readCatalog();

test('the fairness probe tables match the catalog the firmware compiled', () => {
  assert.deepEqual([
    // FACES is how many values a die scores, which for a d% is its hundred outcomes and not the
    // ten faces either of its bodies carries.
    ...tableFailures('FACES', catalog, FACES, (die) => die.outcomes),
    ...tableFailures('BODIES_PER_DIE', catalog, BODIES_PER_DIE, (die) => die.bodiesPerDie),
    ...tableFailures('BODY_FACES', catalog, BODY_FACES, (die) => die.faces),
  ], []);
});

test('every tally and chain size rolls whole dice that fit the tray', () => {
  assert.deepEqual([
    ...bodyCountFailures('TALLY_SIZES', catalog, TALLY_SIZES),
    ...bodyCountFailures('CHAIN_SIZES', catalog, CHAIN_SIZES),
  ], []);
});

test('the device check tables match the catalog the firmware compiled', () => {
  assert.equal(MAX_DICE, catalog.maxDice, 'the tray holds a different number of bodies');
  // The order is checked on its own, and not only the names: the check reaches a type by sending
  // 'T' bytes and counting the presses, so a reordered catalog would measure a die it did not name.
  assert.deepEqual(TYPE_ORDER, catalog.dice.map((die) => die.name));
  const derivedMaxDice = Object.fromEntries(catalog.dice.map((die) => [die.name, maxDiceOf(die.name)]));
  assert.deepEqual([
    // ROLLS is checked for its keys only. Its values are how many times this check rolls each
    // type, a judgement about how long a device run may take, and the catalog has no opinion on it.
    ...tableFailures('ROLLS', catalog, ROLLS),
    ...tableFailures('maxDiceOf', catalog, derivedMaxDice, (die) => die.maxDiceOf),
  ], []);
});
