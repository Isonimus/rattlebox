"""The firmware's version, from git, as a C header (ADR-0021).

`pio_firmware_version.py` runs this before every firmware build. The version is what
`git describe` names the checked-out commit: the release tag itself (`v0.1.0`) on a release, and
otherwise the tag, the commits since it and the commit (`v0.1.0-3-g4a8416d`), with `-dirty` when
the tree has changes, so a log from any build says which source it came from.

A build from a source archive has no git to ask, so it names itself through the RATTLEBOX_VERSION
environment variable, or stops and says so: a made-up version would be the one thing worse than
none in a bug report.
"""
import os
import re
import subprocess

# Release tags are v<major>.<minor>.<patch>; other tags never name a build.
RELEASE_TAG_PATTERN = "v[0-9]*"
# What git describe can print. Anything else would break, or escape, the C string literal.
VERSION_CHARACTERS = re.compile(r"[A-Za-z0-9._+-]+")


def git_describe(project_dir):
    """The checked-out commit as `git describe` names it. Raises if git cannot."""
    return subprocess.check_output(
        ["git", "describe", "--tags", "--match", RELEASE_TAG_PATTERN, "--always", "--dirty"],
        cwd=project_dir,
        text=True,
    ).strip()


VERSION_VARIABLE = "RATTLEBOX_VERSION"


class VersionUnknown(Exception):
    """Neither git nor RATTLEBOX_VERSION can name the build."""


def firmware_version(project_dir, environment):
    """RATTLEBOX_VERSION from @p environment when set, else what git describes."""
    if environment.get(VERSION_VARIABLE):
        return environment[VERSION_VARIABLE]
    try:
        return git_describe(project_dir)
    except (subprocess.CalledProcessError, FileNotFoundError) as error:
        raise VersionUnknown(
            f"git cannot name this build ({error}). Build from a git clone, or set {VERSION_VARIABLE} "
            f"to the release the source came from, e.g. {VERSION_VARIABLE}=v0.1.0 pio run -e cardputer"
        ) from error


def header_text(version):
    """The header that defines RATTLEBOX_VERSION as @p version."""
    if not VERSION_CHARACTERS.fullmatch(version):
        raise ValueError(f"not a version git describe prints: {version!r}")
    return (
        "// Generated before each firmware build by scripts/pio_firmware_version.py (ADR-0021).\n"
        "#pragma once\n"
        f'#define RATTLEBOX_VERSION "{version}"\n'
    )


def write_if_changed(path, text):
    """Writes @p text to @p path unless it already holds it, so an unchanged version rebuilds nothing.

    Returns whether the file was written.
    """
    if os.path.exists(path):
        with open(path, encoding="utf-8") as existing:
            if existing.read() == text:
                return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as header:
        header.write(text)
    return True
