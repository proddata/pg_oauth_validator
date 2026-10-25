"""Regression coverage for the developer container's source snapshot."""

import pathlib
import subprocess
import tempfile
import unittest


COPY_SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "tools/dev/copy-source.py"


class CopySourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = pathlib.Path(self.temporary.name) / "checkout"
        self.destination = pathlib.Path(self.temporary.name) / "snapshot"
        self.source.mkdir()
        subprocess.run(["git", "init", "-q", str(self.source)], check=True)

    def git(self, *args):
        subprocess.run(["git", "-C", str(self.source), *args], check=True)

    def file(self, name, content="source"):
        path = self.source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def copy(self, succeeds=True):
        result = subprocess.run(
            ["python3", str(COPY_SCRIPT), str(self.source), str(self.destination)],
            capture_output=True, text=True,
        )
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def test_copies_working_edits_new_files_and_preserves_names_and_modes(self):
        tracked = self.file("src/changed.c", "old")
        deleted = self.file("src/deleted.c")
        self.git("add", ".")
        tracked.write_text("local edit")
        deleted.unlink()
        executable = self.file("scripts/new file\nwith unicode ü.sh", "#!/bin/sh\n")
        executable.chmod(0o755)
        self.copy()
        self.assertEqual((self.destination / "src/changed.c").read_text(), "local edit")
        self.assertFalse((self.destination / "src/deleted.c").exists())
        copied = self.destination / executable.relative_to(self.source)
        self.assertEqual(copied.read_bytes(), executable.read_bytes())
        self.assertEqual(copied.stat().st_mode & 0o777, 0o755)

    def test_excludes_large_ignored_artifacts_secrets_and_git_metadata(self):
        self.file(".gitignore", "/dist/\n/build/\n/.tools/\n/demo/local/\n*.env\n")
        self.file("src/validator.c")
        artifact = self.file("dist/old-release/image.tar")
        with artifact.open("wb") as stream:
            stream.truncate(128 * 1024 * 1024)
        for name in ("build/pg18/test", ".tools/formatter", "demo/local/provider.yml", ".env"):
            self.file(name, "private fixture")
        self.copy()
        self.assertEqual(
            sorted(str(p.relative_to(self.destination)) for p in self.destination.rglob("*") if p.is_file()),
            [".gitignore", "src/validator.c"],
        )
        self.assertFalse((self.destination / ".git").exists())

    def test_preserves_symlinks_without_copying_their_targets(self):
        outside = pathlib.Path(self.temporary.name) / "outside"
        outside.write_text("not source")
        (self.source / "link").symlink_to(outside)
        (self.source / "broken").symlink_to("absent")
        self.copy()
        self.assertEqual((self.destination / "link").readlink(), outside)
        self.assertEqual((self.destination / "broken").readlink(), pathlib.Path("absent"))

    def test_git_failure_does_not_copy_files(self):
        self.file("src/validator.c")
        # A source archive without Git metadata must not silently copy everything.
        self.source = self.source / "src"
        # Stop Git from finding the parent fixture repository.
        (self.source / ".git").write_text("invalid git metadata\n")
        self.copy(succeeds=False)
        self.assertFalse(self.destination.exists())

    def test_copy_failure_is_not_reported_as_success(self):
        self.file("src/validator.c")
        self.destination.write_text("not a directory")
        self.copy(succeeds=False)


if __name__ == "__main__":
    unittest.main()
