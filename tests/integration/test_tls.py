import os
import pathlib
import subprocess
import tempfile
import time
import uuid
from unittest import mock

import pytest

from integration_support import (
    AUDIENCE,
    LocalIdp,
    TemporaryPostgres,
    b64,
    command,
    psql,
)


TLS_TOKEN_SENTINEL = "tls-private-bearer-token-that-must-not-appear"


@pytest.fixture
def integration_environment():
    return (
        os.environ["PG_CONFIG"],
        os.environ["VALIDATOR_LIBRARY"],
        pathlib.Path(os.environ["OAUTH_TEST_CLIENT"]).resolve(),
    )


def attempt_tls(cluster, client, token, issuer, *, host, root_certificate):
    token_file = cluster.root / f"tls-token-{uuid.uuid4().hex}"
    token_file.write_text(token, encoding="utf-8")
    token_file.chmod(0o600)
    conninfo = (
        f"host={host} port={cluster.port} dbname=postgres user=appuser "
        "sslmode=verify-full "
        f"sslrootcert={root_certificate} oauth_issuer={issuer} "
        "oauth_client_id=test-client connect_timeout=5"
    )
    return command(client, token_file, conninfo, check=False)


def assert_redacted(result, logs, *secrets):
    output = result.stdout + result.stderr + logs
    for secret in secrets:
        assert secret not in output


def create_untrusted_ca(root):
    key = root / "untrusted-ca.key"
    certificate = root / "untrusted-ca.pem"
    subprocess.run(
        [
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", key, "-out", certificate, "-days", "1",
            "-subj", "/CN=Untrusted integration CA",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return certificate


def create_expired_server_certificate(root, name):
    fixture = root / name
    fixture.mkdir()
    ca_key = fixture / "ca.key"
    ca_certificate = fixture / "ca.pem"
    server_key = fixture / "server.key"
    request = fixture / "server.csr"
    server_certificate = fixture / "server.pem"
    config = fixture / "openssl.cnf"
    database = fixture / "index.txt"
    serial = fixture / "serial"
    new_certificates = fixture / "newcerts"
    new_certificates.mkdir()
    database.write_text("", encoding="ascii")
    serial.write_text("01\n", encoding="ascii")
    config.write_text(
        "[ ca ]\n"
        "default_ca = test_ca\n"
        "[ test_ca ]\n"
        f"database = {database}\n"
        f"serial = {serial}\n"
        f"new_certs_dir = {new_certificates}\n"
        f"certificate = {ca_certificate}\n"
        f"private_key = {ca_key}\n"
        "default_md = sha256\n"
        "policy = test_policy\n"
        "x509_extensions = server_certificate\n"
        "[ test_policy ]\n"
        "commonName = supplied\n"
        "[ server_certificate ]\n"
        "basicConstraints = critical,CA:false\n"
        "keyUsage = critical,digitalSignature,keyEncipherment\n"
        "extendedKeyUsage = serverAuth\n"
        "subjectAltName = DNS:localhost\n",
        encoding="ascii",
    )
    quiet = {"check": True, "stdout": subprocess.DEVNULL,
             "stderr": subprocess.DEVNULL}
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
         "-keyout", ca_key, "-out", ca_certificate, "-days", "3650",
         "-subj", "/CN=TLS expiry integration CA"],
        **quiet,
    )
    subprocess.run(
        ["openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes",
         "-keyout", server_key, "-out", request, "-subj", "/CN=localhost"],
        **quiet,
    )
    subprocess.run(
        ["openssl", "ca", "-batch", "-config", config,
         "-startdate", "20200101000000Z", "-enddate", "20200102000000Z",
         "-in", request, "-out", server_certificate],
        **quiet,
    )
    return server_certificate, server_key, ca_certificate


def configured_cluster(environment, idp, *, provider_ca=None):
    pg_config, validator_library, _client = environment
    identity = "v1." + b64(idp.issuer.encode()) + "." + b64(b"principal")
    return TemporaryPostgres(
        pg_config,
        validator_library,
        issuer=idp.issuer,
        identity_map=(identity, "appuser"),
        ca_file=provider_ca or idp.tls_certificate,
        server_certificate=idp.tls_certificate,
        server_key=idp.tls_key,
    )


def test_postgresql_verify_full_tls_matrix(integration_environment):
    _pg_config, _validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-tls-") as directory:
        root = pathlib.Path(directory)
        idp = LocalIdp(root)
        cluster = configured_cluster(integration_environment, idp)
        token = idp.sign() + TLS_TOKEN_SENTINEL
        # Keep a valid token for the accepting case while using a distinct
        # sentinel token for TLS failures and redaction assertions.
        valid_token = idp.sign()
        untrusted_ca = create_untrusted_ca(root)
        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")

            accepted = attempt_tls(
                cluster, client, valid_token, idp.issuer,
                host="localhost", root_certificate=idp.tls_certificate,
            )
            assert accepted.returncode == 0, accepted.stderr + "\n" + cluster.logs()

            for host, ca in (
                ("127.0.0.1", idp.tls_certificate),
                ("localhost", untrusted_ca),
            ):
                started = time.monotonic()
                rejected = attempt_tls(
                    cluster, client, token, idp.issuer,
                    host=host, root_certificate=ca,
                )
                assert time.monotonic() - started < 8
                assert rejected.returncode != 0
                assert_redacted(rejected, cluster.logs(), token, TLS_TOKEN_SENTINEL)
        finally:
            cluster.stop()
            idp.close()


def test_postgresql_expired_certificate_is_bounded_and_redacted(
        integration_environment):
    pg_config, validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-expired-postgres-") as directory:
        root = pathlib.Path(directory)
        idp = LocalIdp(root)
        certificate, key, ca = create_expired_server_certificate(root, "postgres")
        identity = "v1." + b64(idp.issuer.encode()) + "." + b64(b"principal")
        cluster = TemporaryPostgres(
            pg_config,
            validator_library,
            issuer=idp.issuer,
            identity_map=(identity, "appuser"),
            ca_file=idp.tls_certificate,
            server_certificate=certificate,
            server_key=key,
        )
        token = idp.sign() + TLS_TOKEN_SENTINEL
        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")
            started = time.monotonic()
            rejected = attempt_tls(
                cluster, client, token, idp.issuer,
                host="localhost", root_certificate=ca,
            )
            assert time.monotonic() - started < 8
            assert rejected.returncode != 0
            assert_redacted(rejected, cluster.logs(), token, TLS_TOKEN_SENTINEL)
        finally:
            cluster.stop()
            idp.close()


@pytest.mark.parametrize("failure", ["untrusted_ca", "hostname_mismatch"])
def test_provider_tls_failures_are_bounded_and_redacted(
        integration_environment, failure):
    _pg_config, _validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-provider-tls-") as directory:
        root = pathlib.Path(directory)
        idp = LocalIdp(root)
        untrusted_ca = create_untrusted_ca(root)
        if failure == "hostname_mismatch":
            # The server certificate is valid only for localhost. Using the
            # literal loopback address makes libcurl perform a real name check.
            idp.issuer = idp.issuer.replace("localhost", "127.0.0.1")
        provider_ca = untrusted_ca if failure == "untrusted_ca" else idp.tls_certificate
        cluster = configured_cluster(
            integration_environment, idp, provider_ca=provider_ca,
        )
        token = idp.sign() + TLS_TOKEN_SENTINEL
        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")
            started = time.monotonic()
            rejected = attempt_tls(
                cluster, client, token, idp.issuer,
                host="localhost", root_certificate=idp.tls_certificate,
            )
            assert time.monotonic() - started < 8
            assert rejected.returncode != 0
            assert_redacted(rejected, cluster.logs(), token, TLS_TOKEN_SENTINEL)
        finally:
            cluster.stop()
            idp.close()


def test_provider_expired_certificate_is_bounded_and_redacted(
        integration_environment):
    _pg_config, _validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-expired-provider-") as directory:
        root = pathlib.Path(directory)
        certificate, key, ca = create_expired_server_certificate(root, "provider")
        original_run = subprocess.run

        def replace_local_idp_certificate(argv, *args, **kwargs):
            if (len(argv) >= 2 and argv[0:2] == ["openssl", "req"]
                    and "-addext" in argv):
                output = pathlib.Path(argv[argv.index("-out") + 1])
                output_key = pathlib.Path(argv[argv.index("-keyout") + 1])
                output.write_bytes(certificate.read_bytes())
                output_key.write_bytes(key.read_bytes())
                return subprocess.CompletedProcess(argv, 0)
            return original_run(argv, *args, **kwargs)

        with mock.patch("integration_support.subprocess.run",
                        side_effect=replace_local_idp_certificate):
            idp = LocalIdp(root)
        cluster = configured_cluster(integration_environment, idp, provider_ca=ca)
        token = idp.sign() + TLS_TOKEN_SENTINEL
        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")
            started = time.monotonic()
            rejected = attempt_tls(
                cluster, client, token, idp.issuer,
                host="localhost", root_certificate=ca,
            )
            assert time.monotonic() - started < 8
            assert rejected.returncode != 0
            assert_redacted(rejected, cluster.logs(), token, TLS_TOKEN_SENTINEL)
        finally:
            cluster.stop()
            idp.close()
