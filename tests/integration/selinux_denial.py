"""Fresh-cluster denial and AVC evidence before policy install and after removal."""
import os
import pathlib
import re
import tempfile
import time

from integration_support import LocalIdp, TemporaryPostgres, command, psql
from selinux_support import require_enforcing, run


def require(condition, code):
    if not condition:
        raise RuntimeError("SELinux denial check: " + code)


def audit_records():
    # Non-interactive ausearch otherwise reads inherited stdin instead of logs.
    result = run("ausearch", "--input-logs", "-m", "AVC", "--raw", check=False)
    require(result.returncode in (0, 1), "audit_unavailable")
    return set(result.stdout.splitlines())


def matches_https_denial(line, port):
    return all(re.search(pattern, line) for pattern in (
        r"avc:\s+denied\s+\{[^}]*\bname_connect\b[^}]*\}",
        r"\bscontext=\S*:postgresql_t:\S+",
        r"\btcontext=\S*:http_port_t:\S+",
        r"\btclass=tcp_socket\b", r"\bpermissive=0\b",
        rf"\bdest={port}\b",
    ))


def main():
    require(os.environ.get("OAUTH_TEST_SELINUX") == "1", "launcher_required")
    require_enforcing()
    with tempfile.TemporaryDirectory(prefix="pg-oauth-selinux-idp-") as directory:
        idp = LocalIdp(pathlib.Path(directory), cache_max_age=0)
        cluster = None
        try:
            principal = "selinux-private-principal"
            cluster = TemporaryPostgres(
                os.environ["PG_CONFIG"], os.environ["VALIDATOR_LIBRARY"],
                issuer=idp.issuer, identity_map=(principal, "appuser"),
                hba_users="appuser", ca_file=idp.tls_certificate,
                server_certificate=idp.tls_certificate, server_key=idp.tls_key,
            )
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")
            token = idp.sign(subject=principal)
            token_file = cluster.root / "policy-token"
            descriptor = os.open(token_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                stream.write(token)
            # Port-label transactions may rebuild policy. Establish diagnostic
            # visibility after the last transaction, without adding permissions.
            run("semodule", "-DB")
            print(run("auditctl", "-l").stdout, flush=True)
            print(run("sesearch", "--dontaudit", "-s", "postgresql_t",
                      "-t", "http_port_t", "-c", "tcp_socket", "-p", "name_connect").stdout,
                  flush=True)
            previous = audit_records()
            conninfo = (
                f"host=localhost hostaddr=127.0.0.1 port={cluster.port} "
                "dbname=postgres user=appuser sslmode=verify-full "
                f"sslrootcert={idp.tls_certificate} oauth_issuer={idp.issuer} "
                "oauth_client_id=test-client connect_timeout=5"
            )
            result = command(os.environ["OAUTH_TEST_CLIENT"], token_file, conninfo, check=False)
            require(result.returncode != 0, "unexpected_authorization")
            logs = cluster.logs()
            require("http=http_network_failure" in logs, "wrong_failure_stage")
            require(idp.request_counts() == {"metadata": 0, "jwks": 0}, "provider_reached")
            output = result.stdout + result.stderr + logs
            for secret in (token, token.rsplit(".", 1)[1], principal):
                require(secret not in output, "private_data_exposed")
            for _attempt in range(30):
                if any(matches_https_denial(line, idp.server.server_port)
                       for line in audit_records() - previous):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("SELinux denial check: matching_https_avc_missing")
            require_enforcing()
            print("SELinux policy absence: valid token denied; fresh HTTPS AVC and redaction verified")
        finally:
            try:
                if cluster is not None:
                    cluster.stop()
            finally:
                idp.close()


if __name__ == "__main__":
    main()
