// Packages a release from the firmware already built for each board (ADR-0021).
//
// Usage: node scripts/release.mjs --version v0.1.0 --source-url <repo URL> [--out dist]
//        node scripts/release.mjs --check-image <env> [--out dist]
// Build first: pio run -e cardputer && pio run -e m5stickc-plus2
//
// --check-image merges and checks one board's images at whatever version the build was stamped
// with, and writes nothing else. CI runs it on every push, so packaging that breaks fails there
// rather than at the next tag, which is the only other place this script runs.
//
// The release workflow (.github/workflows/release.yml) runs it on a pushed tag. For each board it
// merges the bootloader, partition table, OTA data and app into one image that flashes at 0x0 (for
// esptool, M5Burner and web flashers), and copies the app alone for OTA installers such as
// M5Launcher (ADR-0025). It refuses an image whose firmware does not carry the release's version,
// or whose bootloader header is missing or not DIO. It then writes NOTICES.md, which the licenses
// of the code compiled in ask a binary release to carry, SHA256SUMS.txt, and RELEASE_NOTES.md, the
// version's section of CHANGELOG.md, which the workflow uses as the release's description and which
// must exist.

import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

export const BOARDS = [
  { env: 'cardputer', chip: 'esp32s3', asset: 'cardputer-adv' },
  { env: 'm5stickc-plus2', chip: 'esp32', asset: 'm5stickc-plus2' },
];

const RELEASE_TAG = /^v\d+\.\d+\.\d+$/;
const GENERATED_VERSION = /^#define RATTLEBOX_VERSION "([^"]*)"$/m;

// ESP-IDF's binary partition table: 32-byte entries, each opening with this magic, until an MD5
// entry or erased flash.
const PARTITION_ENTRY_BYTES = 32;
const PARTITION_MAGIC = 0x50aa;
const PARTITION_TYPE_APP = 0x00;
const PARTITION_SUBTYPE_FACTORY = 0x00;
const PARTITION_SUBTYPE_OTA_0 = 0x10;

// Byte 2 of an ESP image header is its SPI flash mode. The boot ROM reads the second-stage
// bootloader in the mode its header names before it can switch the flash to quad, so a merged image
// whose bootloader says QIO boot-loops the board. The Cardputer's board definition builds QIO;
// PlatformIO's uploader rewrites the header to DIO, which merging bypasses. A released image that
// bricked a board on its first flash is how the Adversary learned it (adversary:slice-0014).
const IMAGE_FLASH_MODE_BYTE = 2;
const FLASH_MODE_DIO = 0x02;
// Byte 0 of every ESP image, bootloader or app. Without it at the bootloader offset the flash-mode
// byte read there belongs to no bootloader: a classic ESP32 image is erased flash up to 0x1000, so a
// check at the wrong offset could pass on a stray 0x02 (esp32-wifi-sapper:ADR-0095 checks both).
const IMAGE_MAGIC = 0xe9;

/** The release's version, which must be a tag of the form v<major>.<minor>.<patch>. */
export function checkReleaseVersion(version) {
  if (!RELEASE_TAG.test(version ?? '')) throw new Error(`not a release tag (v<major>.<minor>.<patch>): ${version}`);
  return version;
}

/** The version a build stamped into its firmware, from its generated header (ADR-0021). */
export function stampedVersion(headerText) {
  const match = GENERATED_VERSION.exec(headerText);
  if (!match) throw new Error('the generated firmware_version.h defines no RATTLEBOX_VERSION');
  return match[1];
}

/** Where the bootloader starts the app on a freshly flashed board: the factory app, else ota_0. */
export function appOffset(partitionTable) {
  const apps = [];
  for (let at = 0; at + PARTITION_ENTRY_BYTES <= partitionTable.length; at += PARTITION_ENTRY_BYTES) {
    if (partitionTable.readUInt16LE(at) !== PARTITION_MAGIC) break;
    if (partitionTable[at + 2] === PARTITION_TYPE_APP) {
      apps.push({ subtype: partitionTable[at + 3], offset: partitionTable.readUInt32LE(at + 4) });
    }
  }
  const boot =
    apps.find((app) => app.subtype === PARTITION_SUBTYPE_FACTORY) ??
    apps.find((app) => app.subtype === PARTITION_SUBTYPE_OTA_0);
  if (!boot) throw new Error('the partition table has neither a factory nor an ota_0 app partition');
  return boot.offset;
}

/** Throws unless an ESP image header (`what`, for the message) starts at `offset` in `image`. */
export function checkImageHeader(image, offset, what) {
  if (image[offset] !== IMAGE_MAGIC) {
    throw new Error(`${what}: no ESP image header at 0x${offset.toString(16)}`);
  }
}

/** Throws unless a bootloader whose header names DIO as its flash mode sits at `bootloaderOffset`. */
export function checkBootloaderHeader(mergedImage, bootloaderOffset) {
  checkImageHeader(mergedImage, bootloaderOffset, 'the merged image');
  const mode = mergedImage[bootloaderOffset + IMAGE_FLASH_MODE_BYTE];
  if (mode !== FLASH_MODE_DIO) {
    throw new Error(`the merged bootloader's flash mode is 0x${mode?.toString(16)}, not DIO (0x02): it would not boot`);
  }
}

/** The body of CHANGELOG.md's `## <version>` section: what the release says it changed. */
export function changelogSection(changelog, version) {
  const lines = changelog.split('\n');
  const start = lines.findIndex((line) => line === `## ${version}` || line.startsWith(`## ${version} `));
  if (start === -1) throw new Error(`CHANGELOG.md has no "## ${version}" section`);
  const end = lines.findIndex((line, index) => index > start && line.startsWith('## '));
  const body = lines.slice(start + 1, end === -1 ? undefined : end).join('\n').trim();
  if (body === '') throw new Error(`CHANGELOG.md's "## ${version}" section is empty`);
  return `${body}\n`;
}

/** The SHA256SUMS.txt line for each file, in the format sha256sum -c reads. */
export function checksumLines(files) {
  return files.map(({ name, bytes }) => `${createHash('sha256').update(bytes).digest('hex')}  ${name}\n`).join('');
}

/** NOTICES.md: the source offer, Rattlebox's license, then each notice the release must carry. */
export function noticesText({ version, sourceUrl, license, thirdPartyNotices, attachments }) {
  const sections = [
    `# Rattlebox ${version}: license and notices\n`,
    'The firmware images in this release were built by its release workflow from the source at\n' +
      `${sourceUrl}/tree/${version}. That source, with the build instructions in its README, lets\n` +
      'anyone rebuild the firmware, including against a modified Arduino core for ESP32, as that\n' +
      "core's license (LGPL-2.1) asks. A build from the source archive rather than a git clone names\n" +
      `its version with RATTLEBOX_VERSION=${version}.\n`,
    `## Rattlebox\n\n\`\`\`text\n${license.trimEnd()}\n\`\`\`\n`,
    thirdPartyNotices.trimEnd() + '\n',
    ...attachments.map(({ title, text }) => `## ${title}\n\n\`\`\`text\n${text.trimEnd()}\n\`\`\`\n`),
  ];
  return sections.join('\n');
}

/**
 * The esptool pioarduino installs into its own Python environment under PlatformIO's core directory,
 * from the platform's pinned tool-esptoolpy, and runs to upload. `pio pkg exec -- esptool` looks
 * the name up on PATH, where the package directory's `esptool` is the Python package, not a
 * program: it ran whatever esptool came later on PATH, a user install on a workstation, and failed
 * on CI's clean runner at the v0.1.0 tag.
 */
export function penvEsptool(systemInfo) {
  const coreDir = systemInfo?.core_dir?.value;
  if (typeof coreDir !== 'string' || coreDir === '') {
    throw new Error("pio system info names no core_dir, so the platform's esptool cannot be found");
  }
  return join(coreDir, 'penv', 'bin', 'esptool');
}

function platformEsptool() {
  const esptool = penvEsptool(JSON.parse(run('pio', ['system', 'info', '--json-output'])));
  if (!existsSync(esptool)) throw new Error(`no esptool at ${esptool}: a firmware build installs it, so build first`);
  return esptool;
}

function run(command, args) {
  return execFileSync(command, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'inherit'] });
}

function boardImages(env) {
  const metadata = JSON.parse(run('pio', ['project', 'metadata', '-e', env, '--json-output']))[env];
  const buildDir = dirname(metadata.prog_path);
  const images = metadata.extra.flash_images.map(({ offset, path }) => ({ offset: Number(offset), path }));
  const partitions = images.find(({ path }) => path.endsWith('partitions.bin'));
  if (!partitions) throw new Error(`${env}: no partition table among the flash images`);
  images.push({ offset: appOffset(readFileSync(partitions.path)), path: join(buildDir, 'firmware.bin') });
  return { buildDir, images: images.sort((a, b) => a.offset - b.offset) };
}

// A release packages only firmware stamped with its tag; a check (no releaseVersion) takes the build as
// it was stamped, which on CI is a commit past the last tag.
function packageBoard(board, outDir, releaseVersion) {
  const { buildDir, images } = boardImages(board.env);
  const built = stampedVersion(readFileSync(join(buildDir, 'generated', 'firmware_version.h'), 'utf8'));
  if (releaseVersion !== undefined && built !== releaseVersion) {
    throw new Error(`${board.env}: the firmware was built as ${built}, not ${releaseVersion}`);
  }
  const name = `rattlebox-${built}-${board.asset}.bin`;
  const output = join(outDir, name);
  const offsetArgs = images.flatMap(({ offset, path }) => [`0x${offset.toString(16)}`, path]);
  run(platformEsptool(), ['--chip', board.chip, 'merge-bin', '-o', output,
    '--flash-mode', 'dio', '--flash-freq', 'keep', '--flash-size', 'keep', ...offsetArgs]);
  const merged = readFileSync(output);
  checkBootloaderHeader(merged, images[0].offset);
  // The app alone, for an installer that writes it into an app slot of its own partition table
  // rather than flashing at 0x0 (M5Launcher). The firmware reads no partition, NVS or filesystem,
  // so it runs from whichever slot it lands in (ADR-0025).
  const appName = `rattlebox-${built}-${board.asset}-app.bin`;
  const app = readFileSync(join(buildDir, 'firmware.bin'));
  checkImageHeader(app, 0, appName);
  writeFileSync(join(outDir, appName), app);
  return [{ name, bytes: merged }, { name: appName, bytes: app }];
}

function checkImage(env, outDir) {
  const board = BOARDS.find((candidate) => candidate.env === env);
  if (!board) throw new Error(`--check-image ${env}: not a released board (${BOARDS.map((b) => b.env).join(', ')})`);
  mkdirSync(outDir, { recursive: true });
  for (const { name } of packageBoard(board, outDir)) console.log(`Image ${name} checked in ${outDir}`);
}

function main() {
  const { values } = parseArgs({
    options: {
      version: { type: 'string' },
      'source-url': { type: 'string' },
      out: { type: 'string', default: 'dist' },
      'check-image': { type: 'string' },
    },
  });
  if (values['check-image'] !== undefined) {
    checkImage(values['check-image'], values.out);
    return;
  }
  const version = checkReleaseVersion(values.version);
  if (!values['source-url']) throw new Error('--source-url is required: NOTICES.md offers the source from it');
  // Before the builds are packaged, so a release the changelog does not describe stops first.
  const releaseNotes = changelogSection(readFileSync('CHANGELOG.md', 'utf8'), version);
  mkdirSync(values.out, { recursive: true });

  const files = BOARDS.flatMap((board) => packageBoard(board, values.out, version));
  const lovyanLicense = join('.pio', 'libdeps', BOARDS[0].env, 'LovyanGFX', 'license.txt');
  const notices = noticesText({
    version,
    sourceUrl: values['source-url'].replace(/\/$/, ''),
    license: readFileSync('LICENSE', 'utf8'),
    thirdPartyNotices: readFileSync(join('docs', 'third-party-notices.md'), 'utf8'),
    attachments: [
      { title: 'LovyanGFX', text: readFileSync(lovyanLicense, 'utf8') },
      { title: 'Arduino core for ESP32', text: readFileSync(join('docs', 'licenses', 'arduino-esp32-LICENSE.txt'), 'utf8') },
      { title: 'ESP-IDF', text: readFileSync(join('docs', 'licenses', 'esp-idf-LICENSE.txt'), 'utf8') },
      { title: 'ESP-IDF third-party components', text: readFileSync(join('docs', 'licenses', 'esp-idf-COPYRIGHT.txt'), 'utf8') },
    ],
  });
  files.push({ name: 'NOTICES.md', bytes: Buffer.from(notices) });
  writeFileSync(join(values.out, 'NOTICES.md'), notices);
  writeFileSync(join(values.out, 'SHA256SUMS.txt'), checksumLines(files));
  writeFileSync(join(values.out, 'RELEASE_NOTES.md'), releaseNotes);
  for (const { name } of files) console.log(`  ${name}`);
  console.log(`Release ${version} packaged in ${values.out}`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try {
    main();
  } catch (error) {
    console.error(`FAILED: ${error.message}`);
    process.exit(1);
  }
}
