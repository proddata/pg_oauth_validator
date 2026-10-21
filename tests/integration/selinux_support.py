"""Opt-in disposable-VM launcher; never used by the normal integration path."""
import os
import pathlib
import subprocess
import time
import uuid


def run(*args, check=True):
    return subprocess.run([str(arg) for arg in args], check=check,
                          capture_output=True, text=True, timeout=30)


def require_enforcing():
    if run("getenforce").stdout.strip() != "Enforcing":
        raise RuntimeError("SELinux enforcing mode is required")
    # Global enforcement does not override a per-domain permissive exception.
    policy = run("semanage", "permissive", "-l").stdout.split()
    if "postgresql_t" in policy:
        raise RuntimeError("postgresql_t must not be permissive")


def label_port(port, kind):
    run("semanage", "port", "-a", "-t", kind, "-p", "tcp", str(port))


class ConfinedPostgres:
    def __init__(self, cluster):
        require_enforcing()
        self.cluster = cluster
        self.unit = "pg-oauth-test-" + uuid.uuid4().hex
        self.port_labeled = False
        self.trace = None

    def start(self):
        cluster = self.cluster
        require_enforcing()
        label_port(cluster.port, "postgresql_port_t")
        self.port_labeled = True
        # These temporary test files live under /var/lib/pgsql. Preserve normal
        # policy for installed binaries/libraries; label only this private tree.
        run("chcon", "-R", "-t", "postgresql_db_t", cluster.root)
        # systemd SELinuxContext= (systemd.exec(5)) makes launch failure fatal;
        # executing the packaged postmaster also retains its entrypoint checks.
        try:
            run("systemd-run", "--collect", "--unit=" + self.unit,
                "--service-type=exec", "--uid=postgres",
                "--property=SELinuxContext=system_u:system_r:postgresql_t:s0",
                "--property=StandardOutput=journal",
                "--property=StandardError=journal",
                cluster.bindir / "postgres", "-D", cluster.data)
        except subprocess.CalledProcessError as error:
            journal = run("journalctl", "--unit=" + self.unit, "--no-pager",
                          "-n", "20", check=False).stdout
            # No clients have connected yet; these are startup diagnostics.
            raise RuntimeError("confined PostgreSQL launch failed: " +
                               error.stderr + journal) from error
        for _attempt in range(100):
            status = run(cluster.bindir / "pg_isready", "-h", cluster.root,
                         "-p", cluster.port, check=False)
            if status.returncode == 0:
                pid = int((cluster.data / "postmaster.pid").read_text().splitlines()[0])
                context = pathlib.Path(f"/proc/{pid}/attr/current").read_text().strip().strip("\0")
                if context != "system_u:system_r:postgresql_t:s0":
                    raise RuntimeError("postmaster escaped the required SELinux domain")
                require_enforcing()
                print(f"SELinux postmaster: {context}", flush=True)
                if os.environ.get("OAUTH_TEST_NETWORK_TRACE") == "1":
                    # Diagnostic only: never trace read/write, token bytes, or TLS.
                    self.trace = subprocess.Popen(
                        ["strace", "-ff", "-e", "trace=connect,socket,getsockopt",
                         "-p", str(pid), "-o", str(cluster.root / "network.trace")],
                        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                    )
                    time.sleep(0.2)
                return
            if run("systemctl", "is-active", self.unit, check=False).returncode:
                break
            time.sleep(0.1)
        raise RuntimeError("confined PostgreSQL failed to start: " + cluster.logs())

    def logs(self):
        # PostgreSQL keeps its collector off; journald receives stderr directly.
        # Flush pending journal writes before the existing redaction assertions.
        run("journalctl", "--sync")
        return run("journalctl", "--unit=" + self.unit, "--output=cat",
                   "--no-pager").stdout

    def stop(self):
        if self.trace is not None:
            self.trace.terminate()
            self.trace.wait(timeout=10)
            for path in sorted(self.cluster.root.glob("network.trace.*")):
                print(path.read_text()[:16384], flush=True)
            self.trace = None
        run("systemctl", "stop", self.unit, check=False)
        run("systemctl", "reset-failed", self.unit, check=False)
        if self.port_labeled:
            run("semanage", "port", "-d", "-p", "tcp", self.cluster.port)
            self.port_labeled = False
