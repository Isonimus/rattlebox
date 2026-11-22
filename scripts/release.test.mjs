import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';

import {
  appOffset,
  changelogSection,
  checkBootloaderHeader,
  checkImageHeader,
  checkReleaseVersion,
  checksumLines,
  noticesText,
  penvEsptool,
  stampedVersion,
} from './release.mjs';

// The StickC build's own table: the core's default_8MB.csv, app0 (ota_0) at 0x10000 (ADR-0020).
const DEFAULT_8MB_TABLE = readFileSync(new URL('./fixtures/partitions-default-8MB.bin', import.meta.url));

/** A binary partition table holding @p entries, then erased flash. */
function partitionTable(entries) {
  const table = Buffer.alloc(32 * (entries.length + 1), 0xff);
  entries.forEach(({ type, subtype, offset }, index) => {
    const at = index * 32;
    table.writeUInt16LE(0x50aa, at);
    table[at + 2] = type;
    table[at + 3] = subtype;
    table.writeUInt32LE(offset, at + 4);
  });
  return table;
}

test('the release version must be a v<major>.<minor>.<patch> tag', () => {
  assert.equal(checkReleaseVersion('v0.1.0'), 'v0.1.0');
  for (const version of ['0.1.0', 'v0.1', 'v0.1.0-dirty', 'v0.1.0-3-g4a8416d', '', undefined]) {
    assert.throws(() => checkReleaseVersion(version), /not a release tag/, String(version));
  }
});

test("the stamped version is read from the build's generated header", () => {
  const header = '// Generated\n#pragma once\n#define RATTLEBOX_VERSION "v0.1.0"\n';
  assert.equal(stampedVersion(header), 'v0.1.0');
  assert.throws(() => stampedVersion('#pragma once\n'), /defines no RATTLEBOX_VERSION/);
});

test("the app offset of the core's default 8 MB table is ota_0's", () => {
  assert.equal(appOffset(DEFAULT_8MB_TABLE), 0x10000);
});

test('a factory app is booted before ota_0, wherever each sits', () => {
  const table = partitionTable([
    { type: 0x01, subtype: 0x02, offset: 0x9000 },
    { type: 0x00, subtype: 0x10, offset: 0x10000 },
    { type: 0x00, subtype: 0x00, offset: 0x200000 },
  ]);
  assert.equal(appOffset(table), 0x200000);
});

test('a table with no bootable app partition is refused', () => {
  const dataOnly = partitionTable([{ type: 0x01, subtype: 0x02, offset: 0x9000 }]);
  assert.throws(() => appOffset(dataOnly), /neither a factory nor an ota_0/);
  const ota1Only = partitionTable([{ type: 0x00, subtype: 0x11, offset: 0x10000 }]);
  assert.throws(() => appOffset(ota1Only), /neither a factory nor an ota_0/);
  assert.throws(() => appOffset(Buffer.alloc(0)), /neither a factory nor an ota_0/);
});

test("entries after the table's end are not read as partitions", () => {
  const table = Buffer.concat([
    partitionTable([{ type: 0x01, subtype: 0x02, offset: 0x9000 }]),
    partitionTable([{ type: 0x00, subtype: 0x00, offset: 0x10000 }]),
  ]);
  assert.throws(() => appOffset(table), /neither a factory nor an ota_0/);
});

test('a merged image is accepted only when a DIO bootloader header sits at the bootloader offset', () => {
  const image = Buffer.alloc(0x1010, 0xff);
  image[0x1000] = 0xe9;
  image[0x1000 + 2] = 0x02;
  assert.doesNotThrow(() => checkBootloaderHeader(image, 0x1000));
  image[0x1000 + 2] = 0x00; // QIO: the boot ROM misreads the bootloader and resets for good
  assert.throws(() => checkBootloaderHeader(image, 0x1000), /0x0, not DIO/);
  assert.throws(() => checkBootloaderHeader(Buffer.alloc(1), 0x1000), /no ESP image/);
});

test('a DIO byte with no image header there is refused: the bootloader is not at that offset', () => {
  // A classic ESP32 image is erased flash from 0x0 to its bootloader at 0x1000. Read at the wrong
  // offset, a stray 0x02 must not pass for a DIO bootloader.
  const image = Buffer.alloc(0x1010, 0xff);
  image[2] = 0x02;
  assert.throws(() => checkBootloaderHeader(image, 0), /no ESP image header at 0x0/);
});

test('an app image is accepted only when it opens with an ESP image header', () => {
  assert.doesNotThrow(() => checkImageHeader(Buffer.from([0xe9, 0x05, 0x02, 0x2f]), 0, 'the app'));
  assert.throws(() => checkImageHeader(Buffer.from([0xff, 0xff]), 0, 'the app'), /the app: no ESP image header at 0x0/);
  assert.throws(() => checkImageHeader(Buffer.alloc(0), 0, 'the app'), /no ESP image header/);
});

test('checksum lines are what sha256sum -c reads', () => {
  const bytes = Buffer.from('rattle');
  const digest = createHash('sha256').update(bytes).digest('hex');
  assert.equal(checksumLines([{ name: 'a.bin', bytes }]), `${digest}  a.bin\n`);
});

test('the notices offer the tagged source, then carry every license given', () => {
  const text = noticesText({
    version: 'v0.1.0',
    sourceUrl: 'https://example.invalid/rattlebox',
    license: 'MIT License\n',
    thirdPartyNotices: '# Third-party notices\n',
    attachments: [{ title: 'LovyanGFX', text: 'FreeBSD license' }],
  });
  assert.match(text, /^# Rattlebox v0\.1\.0: license and notices/);
  assert.match(text, /https:\/\/example\.invalid\/rattlebox\/tree\/v0\.1\.0/);
  assert.match(text, /LGPL-2\.1/);
  assert.match(text, /## Rattlebox\n\n```text\nMIT License\n```/);
  assert.match(text, /# Third-party notices/);
  assert.match(text, /## LovyanGFX\n\n```text\nFreeBSD license\n```/);
});

test("the release notes are the version's changelog section, up to the next one", () => {
  const changelog = '# Changelog\n\n## v0.2.0 (2027-01-01)\n\n- Newer.\n\n## v0.1.0 (2026-11-22)\n\n- First.\n- Second.\n';
  assert.equal(changelogSection(changelog, 'v0.1.0'), '- First.\n- Second.\n');
  assert.equal(changelogSection(changelog, 'v0.2.0'), '- Newer.\n');
});

test('a version the changelog does not describe is refused', () => {
  assert.throws(() => changelogSection('# Changelog\n\n## v0.1.0\n\n- First.\n', 'v0.1.1'), /no "## v0.1.1" section/);
  assert.throws(() => changelogSection('## v0.1.0\n\n## v0.0.9\n- Old.\n', 'v0.1.0'), /section is empty/);
  assert.throws(() => changelogSection('## v0.1.00\n- Not it.\n', 'v0.1.0'), /no "## v0.1.0" section/);
});

test("esptool is the one pioarduino installed under PlatformIO's core directory", () => {
  const systemInfo = { core_dir: { title: 'PlatformIO Core Directory', value: join('project', '.platformio') } };
  assert.equal(penvEsptool(systemInfo), join('project', '.platformio', 'penv', 'bin', 'esptool'));
});

test('system info without a core directory is refused rather than guessed', () => {
  for (const systemInfo of [{}, { core_dir: {} }, { core_dir: { value: '' } }]) {
    assert.throws(() => penvEsptool(systemInfo), /no core_dir/);
  }
});
