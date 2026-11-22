"""PlatformIO pre-script: writes the firmware version header into the build (ADR-0021).

A header in the build directory rather than a -D flag: a flag that changes with every commit
changes every compile command, so each commit would rebuild the whole firmware; the header,
rewritten only when the version changes, rebuilds only what includes it.
"""
import os
import sys

Import("env")  # noqa: F821 (SCons provides Import to PlatformIO scripts)

PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
sys.path.insert(0, os.path.join(PROJECT_DIR, "scripts"))

from firmware_version import VersionUnknown, firmware_version, header_text, write_if_changed  # noqa: E402

GENERATED_DIR = os.path.join(env.subst("$BUILD_DIR"), "generated")  # noqa: F821
try:
    VERSION = firmware_version(PROJECT_DIR, os.environ)
except VersionUnknown as error:
    # The reason alone, not a traceback from inside SCons, and the build stops.
    sys.stderr.write(f"firmware version: {error}\n")
    env.Exit(1)  # noqa: F821
write_if_changed(os.path.join(GENERATED_DIR, "firmware_version.h"), header_text(VERSION))
env.Append(CPPPATH=[GENERATED_DIR])  # noqa: F821
