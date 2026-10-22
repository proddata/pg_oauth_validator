import copy
import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest
import sys
from unittest import mock

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("builder", ROOT / "scripts/ci/rpm-builder.py")
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)


class BuilderTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.payload = self.root / "payload"
        self.payload.mkdir()
        for name in builder.MEMBERS:
            (self.payload / name).write_bytes(b"synthetic builder fixture\n")
        self.entry = {
            "target": "fedora44-arm64", "image_id": "sha256:" + "a" * 64,
            "base_image": "fedora:44@sha256:" + "b" * 64,
            "capture_revision": "c" * 40, "artifact_id": 12, "run_id": 34,
            "files": {name: {"sha256": hashlib.sha256((self.payload / name).read_bytes()).hexdigest(),
                             "bytes": (self.payload / name).stat().st_size}
                      for name in builder.MEMBERS},
        }
        self.info = {"Id": self.entry["image_id"], "Os": "linux", "Architecture": "arm64",
                     "Config": {"Labels": {"org.opencontainers.image.revision": "c" * 40,
                                            "org.opencontainers.image.base.name": self.entry["base_image"]}}}

    def test_verified_archive_loaded_by_content_identity(self):
        with mock.patch.object(builder.platform, "machine", return_value="aarch64"), \
                mock.patch.object(builder.subprocess, "run") as run, \
                mock.patch.object(builder, "inspect", return_value=self.info) as inspect:
            builder.load(self.entry, self.payload)
            self.assertEqual(run.call_args.args[0], ["docker", "image", "load", "--input",
                                                    str(self.payload / "builder.tar.gz")])
            inspect.assert_called_once_with(self.entry["image_id"])

    def test_corrupt_missing_or_linked_files_never_reach_docker(self):
        for name in builder.MEMBERS:
            for kind in ("corrupt", "missing", "symlink"):
                with self.subTest(name=name, kind=kind):
                    path = self.payload / name
                    original = path.read_bytes()
                    path.unlink()
                    if kind == "corrupt":
                        path.write_bytes(b"changed")
                    elif kind == "symlink":
                        target = self.root / "target"
                        target.write_bytes(original)
                        path.symlink_to(target)
                    with mock.patch.object(builder.subprocess, "run") as run:
                        with self.assertRaises(ValueError):
                            builder.load(self.entry, self.payload)
                        run.assert_not_called()
                    if path.exists() or path.is_symlink():
                        path.unlink()
                    path.write_bytes(original)

    def test_wrong_architecture_does_not_load_archive(self):
        with mock.patch.object(builder.platform, "machine", return_value="x86_64"), \
                mock.patch.object(builder.subprocess, "run") as run:
            with self.assertRaises(ValueError):
                builder.load(self.entry, self.payload)
            run.assert_not_called()

    def test_wrong_loaded_identity_or_source_is_rejected(self):
        for key, value in (("Id", "sha256:" + "d" * 64), ("Architecture", "amd64"),
                           ("Os", "windows"), ("Config", {"Labels": {}})):
            info = {**self.info, key: value}
            with self.subTest(key=key), \
                    mock.patch.object(builder.platform, "machine", return_value="arm64"), \
                    mock.patch.object(builder.subprocess, "run"), \
                    mock.patch.object(builder, "inspect", return_value=info):
                with self.assertRaises(ValueError):
                    builder.load(self.entry, self.payload)

    def test_malformed_metadata_fails_closed(self):
        for key, value in (("image_id", "latest"), ("target", []), ("target", "fedora42-amd64"),
                           ("capture_revision", "HEAD"), ("artifact_id", 0), ("run_id", True),
                           ("base_image", "fedora:latest"), ("files", {})):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                builder.validate({**self.entry, key: value})
        for size in (0, -1, True, builder.MAX_ARCHIVE + 1):
            entry = copy.deepcopy(self.entry)
            entry["files"]["builder.tar.gz"]["bytes"] = size
            with self.subTest(size=size), self.assertRaises(ValueError):
                builder.validate(entry)

    def test_full_lock_rejects_missing_duplicate_and_mismatched_targets(self):
        entries = {target: {**self.entry, "target": target} for target in builder.TARGETS}
        lock = self.root / "lock.json"
        lock.write_text(json.dumps({"format_version": 1, "builders": entries}))
        self.assertEqual(set(builder.lock_entries(lock)), builder.TARGETS)
        entries.pop("fedora43-amd64")
        lock.write_text(json.dumps({"format_version": 1, "builders": entries}))
        with self.assertRaises(ValueError):
            builder.lock_entries(lock)
        lock.write_text('{"format_version":1,"format_version":1,"builders":{}}')
        with self.assertRaises(ValueError):
            builder.lock_entries(lock)

    def test_unregistered_capture_cannot_be_consumed(self):
        entry = {**self.entry, "artifact_id": None, "run_id": None}
        builder.validate(entry, registered=False)
        with self.assertRaises(ValueError):
            builder.verify(entry, self.payload)

    def test_extra_files_and_existing_outputs_are_rejected(self):
        (self.payload / "unexpected").write_text("extra")
        with self.assertRaises(ValueError):
            builder.verify(self.entry, self.payload)
        path = self.root / "keep.json"
        path.write_text("retain")
        with self.assertRaises(FileExistsError):
            builder.write_json(path, self.entry)
        self.assertEqual(path.read_text(), "retain")


if __name__ == "__main__":
    unittest.main()
