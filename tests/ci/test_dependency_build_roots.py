import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class BuildRootTest(unittest.TestCase):
    def test_existing_or_relative_build_roots_are_rejected_without_changes(self):
        for dependency in ("jansson", "libjwt"):
            for kind in ("directory", "file", "symlink", "relative"):
                with self.subTest(dependency=dependency, kind=kind), tempfile.TemporaryDirectory() as directory:
                    root = pathlib.Path(directory)
                    target = root / "retained"
                    target.mkdir()
                    marker = target / "marker"
                    marker.write_text("preserve")
                    work = root / "build"
                    if kind == "directory":
                        work.mkdir()
                    elif kind == "file":
                        work.write_text("preserve")
                    elif kind == "symlink":
                        work.symlink_to(target, target_is_directory=True)
                    environment = dict(os.environ)
                    environment[dependency.upper() + "_BUILD_ROOT"] = "relative-build-root" if kind == "relative" else str(work)
                    result = subprocess.run([
                        "sh", str(ROOT / f"scripts/ci/install-{dependency}.sh"),
                    ], cwd=root, env=environment, capture_output=True, text=True, timeout=10)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(marker.read_text(), "preserve")
                    if kind == "symlink":
                        self.assertTrue(work.is_symlink())
                    elif kind == "file":
                        self.assertEqual(work.read_text(), "preserve")
                    elif kind == "directory":
                        self.assertTrue(work.is_dir())
                    else:
                        self.assertFalse((root / "relative-build-root").exists())


if __name__ == "__main__":
    unittest.main()
