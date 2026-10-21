import pathlib
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/integration"))
import selinux_support


class EnforcementTest(unittest.TestCase):
    def result(self, output):
        return subprocess.CompletedProcess([], 0, output, "")

    def test_enforcing_domain_is_accepted(self):
        with mock.patch.object(selinux_support, "run", side_effect=[
            self.result("Enforcing\n"), self.result("Builtin Permissive Types\n")
        ]):
            selinux_support.require_enforcing()

    def test_disabled_or_globally_permissive_is_rejected(self):
        for value in ["Disabled", "Permissive", "", "enforcing"]:
            with self.subTest(value=value), mock.patch.object(
                selinux_support, "run", return_value=self.result(value)
            ), self.assertRaises(RuntimeError):
                selinux_support.require_enforcing()

    def test_permissive_postgresql_is_rejected(self):
        with mock.patch.object(selinux_support, "run", side_effect=[
            self.result("Enforcing\n"),
            self.result("Customized Permissive Types\npostgresql_t\n")
        ]), self.assertRaises(RuntimeError):
            selinux_support.require_enforcing()

    def test_missing_policy_tools_fail_closed(self):
        with mock.patch.object(selinux_support, "run", side_effect=FileNotFoundError), \
                self.assertRaises(FileNotFoundError):
            selinux_support.require_enforcing()


class LauncherTest(unittest.TestCase):
    def test_failed_launch_still_removes_its_port_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            cluster = types.SimpleNamespace(root=root, log=root / "postgres.log",
                                            port=54321, bindir=root, data=root / "data",
                                            logs=lambda: "")
            with mock.patch.object(selinux_support, "require_enforcing"), \
                    mock.patch.object(selinux_support, "label_port") as label, \
                    mock.patch.object(selinux_support, "run") as run:
                launcher = selinux_support.ConfinedPostgres(cluster)
                run.side_effect = [None, None,
                                   subprocess.CalledProcessError(1, "systemd-run", stderr="failed"),
                                   subprocess.CompletedProcess([], 0, "startup journal", "")]
                with self.assertRaisesRegex(RuntimeError, "startup journal"):
                    launcher.start()
                label.assert_called_once_with(54321, "postgresql_port_t")
                run.side_effect = None
                launcher.stop()
                self.assertIn(mock.call("semanage", "port", "-d", "-p", "tcp", 54321),
                              run.call_args_list)
                self.assertFalse(launcher.port_labeled)


if __name__ == "__main__":
    unittest.main()
