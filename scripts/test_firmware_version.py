"""Tests for firmware_version.py (ADR-0021). Run: python3 -m unittest discover -s scripts"""
import os
import subprocess
import tempfile
import unittest

from firmware_version import VersionUnknown, firmware_version, git_describe, header_text, write_if_changed


def git(repo, *args):
    subprocess.run(["git", *args], cwd=repo, check=True, capture_output=True)


class GitDescribeTest(unittest.TestCase):
    def setUp(self):
        self.repo = tempfile.mkdtemp()
        git(self.repo, "init", "-q")
        git(self.repo, "config", "user.email", "test@example.invalid")
        git(self.repo, "config", "user.name", "test")
        git(self.repo, "config", "commit.gpgsign", "false")
        self.tracked = os.path.join(self.repo, "file.txt")
        self.commit("first")

    def commit(self, content):
        with open(self.tracked, "w", encoding="utf-8") as tracked:
            tracked.write(content)
        git(self.repo, "add", "file.txt")
        git(self.repo, "commit", "-q", "-m", content)

    def test_a_clean_tagged_commit_is_its_tag(self):
        git(self.repo, "tag", "v0.1.0")
        self.assertEqual(git_describe(self.repo), "v0.1.0")

    def test_changes_mark_the_version_dirty(self):
        git(self.repo, "tag", "v0.1.0")
        with open(self.tracked, "w", encoding="utf-8") as tracked:
            tracked.write("changed")
        self.assertEqual(git_describe(self.repo), "v0.1.0-dirty")

    def test_commits_after_the_tag_are_counted_and_named(self):
        git(self.repo, "tag", "v0.1.0")
        self.commit("second")
        self.assertRegex(git_describe(self.repo), r"^v0\.1\.0-1-g[0-9a-f]+$")

    def test_a_tag_that_is_not_a_release_is_ignored(self):
        git(self.repo, "tag", "before-release")
        self.assertRegex(git_describe(self.repo), r"^[0-9a-f]+$")

    def test_outside_a_repository_it_fails(self):
        with self.assertRaises(subprocess.CalledProcessError):
            git_describe(tempfile.mkdtemp())


class FirmwareVersionTest(unittest.TestCase):
    def test_a_checkout_is_named_by_git(self):
        repo = tempfile.mkdtemp()
        git(repo, "init", "-q")
        git(repo, "config", "user.email", "test@example.invalid")
        git(repo, "config", "user.name", "test")
        git(repo, "commit", "-q", "--allow-empty", "-m", "first")
        git(repo, "tag", "v0.1.0")
        self.assertEqual(firmware_version(repo, {}), "v0.1.0")

    def test_the_variable_names_a_build_without_git(self):
        self.assertEqual(firmware_version(tempfile.mkdtemp(), {"RATTLEBOX_VERSION": "v0.1.0"}), "v0.1.0")

    def test_the_variable_wins_over_git(self):
        repo = tempfile.mkdtemp()
        git(repo, "init", "-q")
        self.assertEqual(firmware_version(repo, {"RATTLEBOX_VERSION": "v0.1.0"}), "v0.1.0")

    def test_without_git_or_the_variable_it_says_how_to_name_the_build(self):
        with self.assertRaisesRegex(VersionUnknown, "RATTLEBOX_VERSION"):
            firmware_version(tempfile.mkdtemp(), {})


class HeaderTextTest(unittest.TestCase):
    def test_defines_the_version_as_a_string(self):
        self.assertIn('#define RATTLEBOX_VERSION "v0.1.0-3-g4a8416d-dirty"\n', header_text("v0.1.0-3-g4a8416d-dirty"))

    def test_rejects_what_would_break_the_string_literal(self):
        for version in ['v0.1"', "v0.1\\", "", "v 0.1", "v0.1\n"]:
            with self.subTest(version=version), self.assertRaises(ValueError):
                header_text(version)


class WriteIfChangedTest(unittest.TestCase):
    def test_writes_a_new_file_and_its_directory(self):
        path = os.path.join(tempfile.mkdtemp(), "generated", "firmware_version.h")
        self.assertTrue(write_if_changed(path, "a"))
        with open(path, encoding="utf-8") as written:
            self.assertEqual(written.read(), "a")

    def test_leaves_a_file_that_already_holds_the_text(self):
        path = os.path.join(tempfile.mkdtemp(), "firmware_version.h")
        write_if_changed(path, "a")
        before = os.stat(path).st_mtime_ns
        self.assertFalse(write_if_changed(path, "a"))
        self.assertEqual(os.stat(path).st_mtime_ns, before)

    def test_rewrites_a_file_that_holds_another_version(self):
        path = os.path.join(tempfile.mkdtemp(), "firmware_version.h")
        write_if_changed(path, "a")
        self.assertTrue(write_if_changed(path, "b"))
        with open(path, encoding="utf-8") as written:
            self.assertEqual(written.read(), "b")


if __name__ == "__main__":
    unittest.main()
