import concurrent.futures
import os
import pathlib
import subprocess
import tempfile
import time
import uuid

import pytest

from integration_support import (
    AUDIENCE,
    IDP_RESPONSE_SECRET,
    ISSUER,
    TOKEN,
    LocalIdp,
    TemporaryPostgres,
    attempt_oauth,
    b64,
    psql,
    psql_argv,
    reload_setting,
    wait_setting,
)


@pytest.fixture
def integration_environment():
    pg_config = os.environ["PG_CONFIG"]
    validator_library = os.environ["VALIDATOR_LIBRARY"]
    client = pathlib.Path(os.environ["OAUTH_TEST_CLIENT"]).resolve()
    return pg_config, validator_library, client


@pytest.mark.parametrize("policy", [None, "default"])
def test_real_oauth_connection_fails_closed(integration_environment, policy):
    pg_config, validator_library, client = integration_environment
    cluster = TemporaryPostgres(pg_config, validator_library, policy=policy)
    if policy is not None and cluster.major < 19:
        pytest.skip("validator.policy is available in PostgreSQL 19 and later")

    try:
        cluster.start()
        result = attempt_oauth(cluster, client)
        logs = cluster.logs()

        assert result.returncode != 0
        assert TOKEN not in result.stdout
        assert TOKEN not in result.stderr
        assert TOKEN not in logs
        assert "pg_oauth_validator" in logs
    finally:
        cluster.stop()

def test_pg19_unknown_policy_fails_closed(integration_environment):
    pg_config, validator_library, client = integration_environment
    cluster = TemporaryPostgres(pg_config, validator_library, policy="unknown")
    if cluster.major < 19:
        pytest.skip("custom validator options are available in PostgreSQL 19 and later")

    try:
        cluster.start()
        result = attempt_oauth(cluster, client)
        logs = cluster.logs()

        assert result.returncode != 0
        assert TOKEN not in result.stderr
        assert TOKEN not in logs
    finally:
        cluster.stop()


def test_missing_audience_fails_closed(integration_environment):
    pg_config, validator_library, client = integration_environment
    cluster = TemporaryPostgres(pg_config, validator_library, audience="")

    try:
        cluster.start()
        result = attempt_oauth(cluster, client)
        logs = cluster.logs()

        assert result.returncode != 0
        assert TOKEN not in result.stderr
        assert TOKEN not in logs
    finally:
        cluster.stop()


def test_valid_token_uses_normal_pg_ident_mapping(integration_environment):
    pg_config, validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-idp-") as directory:
        idp = LocalIdp(pathlib.Path(directory))
        identity = "v1." + b64(idp.issuer.encode()) + "." + b64(b"principal")
        cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            identity_map=(identity, "appuser"), ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        valid = idp.sign()
        insufficient_scope = idp.sign(scope="read:metadata")
        previous_ca = os.environ.get("SSL_CERT_FILE")
        os.environ["SSL_CERT_FILE"] = str(idp.tls_certificate)
        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")

            accepted = attempt_oauth(
                cluster, client, token=valid, user="appuser", issuer=idp.issuer,
                sslmode="require",
            )
            assert accepted.returncode == 0, accepted.stderr + "\n" + cluster.logs()

            psql(cluster, "ALTER SYSTEM SET pg_oauth_validator.audiences "
                          "TO 'https://other-resource.example/'")
            psql(cluster, "SELECT pg_reload_conf()")
            wait_setting(cluster, "pg_oauth_validator.audiences",
                         "https://other-resource.example/")
            rejected_after_reload = attempt_oauth(
                cluster, client, token=valid, user="appuser", issuer=idp.issuer,
                sslmode="require",
            )
            assert rejected_after_reload.returncode != 0

            psql(cluster, "ALTER SYSTEM SET pg_oauth_validator.audiences "
                          f"TO '{AUDIENCE}'")
            psql(cluster, "SELECT pg_reload_conf()")
            wait_setting(cluster, "pg_oauth_validator.audiences", AUDIENCE)
            accepted_after_reload = attempt_oauth(
                cluster, client, token=valid, user="appuser", issuer=idp.issuer,
                sslmode="require",
            )
            assert accepted_after_reload.returncode == 0

            administrative = attempt_oauth(
                cluster, client, token=valid, user="postgres", issuer=idp.issuer,
                sslmode="require",
            )
            assert administrative.returncode != 0

            hba_path = cluster.data / "pg_hba.conf"
            hba = hba_path.read_text(encoding="utf-8")
            hba_path.write_text(
                hba.replace("map=oauthmap", "delegate_ident_mapping=1"),
                encoding="utf-8",
            )
            psql(cluster, "SELECT pg_reload_conf()")
            hba_errors = psql(
                cluster,
                "SELECT error FROM pg_hba_file_rules WHERE error IS NOT NULL",
            )
            assert hba_errors.stdout.strip() == ""
            delegated = attempt_oauth(
                cluster, client, token=valid, user="postgres", issuer=idp.issuer,
                sslmode="require",
            )
            assert delegated.returncode != 0

            rejected_scope = attempt_oauth(
                cluster, client, token=insufficient_scope,
                user="appuser", issuer=idp.issuer,
                sslmode="require",
            )
            assert rejected_scope.returncode != 0

            logs = cluster.logs()
            for secret in (valid, insufficient_scope):
                assert secret not in logs
                assert secret not in administrative.stderr
                assert secret not in rejected_scope.stderr
                assert secret not in rejected_after_reload.stderr
                assert secret not in delegated.stderr
        finally:
            cluster.stop()
            idp.close()
            if previous_ca is None:
                os.environ.pop("SSL_CERT_FILE", None)
            else:
                os.environ["SSL_CERT_FILE"] = previous_ca


def test_direct_identity_and_delegated_roles(integration_environment):
    pg_config, validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-idp-roles-") as directory:
        idp = LocalIdp(pathlib.Path(directory))
        direct_cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            identity_format="direct", ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        delegated_cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            identity_format="issuer_qualified",
            authorization_mode="claim_roles", delegate_ident_mapping=True,
            hba_users="app_reader,reporting,postgres", ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        policy_only_cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            authorization_mode="claim_roles", delegate_ident_mapping=False,
            hba_users="reporting", ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        hba_only_cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            authorization_mode="identity", delegate_ident_mapping=True,
            hba_users="reporting", ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        direct_token = idp.sign(subject="provider-subject")
        delegated_token = idp.sign(
            extra_claims={"roles": ["app_reader", "reporting"]},
        )
        insufficient_roles = idp.sign(
            extra_claims={"roles": ["app_reader"]},
        )
        privileged_token = idp.sign(
            extra_claims={"roles": ["postgres"]},
        )
        duplicate_roles = idp.sign(
            extra_claims={"roles": ["reporting", "reporting"]},
        )
        previous_ca = os.environ.get("SSL_CERT_FILE")
        os.environ["SSL_CERT_FILE"] = str(idp.tls_certificate)
        try:
            direct_cluster.start()
            psql(direct_cluster, 'CREATE ROLE "provider-subject" LOGIN')
            accepted_direct = attempt_oauth(
                direct_cluster, client, token=direct_token,
                user="provider-subject", issuer=idp.issuer, sslmode="require",
            )
            assert accepted_direct.returncode == 0, (
                accepted_direct.stderr + "\n" + direct_cluster.logs()
            )
            assert attempt_oauth(
                direct_cluster, client, token=direct_token,
                user="postgres", issuer=idp.issuer, sslmode="require",
            ).returncode != 0

            delegated_cluster.start()
            psql(delegated_cluster, "CREATE ROLE app_reader LOGIN")
            psql(delegated_cluster, "CREATE ROLE reporting LOGIN")
            accepted_delegated = attempt_oauth(
                delegated_cluster, client, token=delegated_token,
                user="reporting", issuer=idp.issuer, sslmode="require",
            )
            assert accepted_delegated.returncode == 0, (
                accepted_delegated.stderr + "\n" + delegated_cluster.logs()
            )
            assert attempt_oauth(
                delegated_cluster, client, token=delegated_token,
                user="postgres", issuer=idp.issuer, sslmode="require",
            ).returncode != 0
            accepted_privileged = attempt_oauth(
                delegated_cluster, client, token=privileged_token,
                user="postgres", issuer=idp.issuer, sslmode="require",
            )
            assert accepted_privileged.returncode == 0, (
                accepted_privileged.stderr + "\n" + delegated_cluster.logs()
            )
            assert attempt_oauth(
                delegated_cluster, client, token=insufficient_roles,
                user="reporting", issuer=idp.issuer, sslmode="require",
            ).returncode != 0
            assert attempt_oauth(
                delegated_cluster, client, token=duplicate_roles,
                user="reporting", issuer=idp.issuer, sslmode="require",
            ).returncode != 0

            policy_only_cluster.start()
            psql(policy_only_cluster, "CREATE ROLE reporting LOGIN")
            assert attempt_oauth(
                policy_only_cluster, client, token=delegated_token,
                user="reporting", issuer=idp.issuer, sslmode="require",
            ).returncode != 0

            hba_only_cluster.start()
            psql(hba_only_cluster, "CREATE ROLE reporting LOGIN")
            assert attempt_oauth(
                hba_only_cluster, client, token=delegated_token,
                user="reporting", issuer=idp.issuer, sslmode="require",
            ).returncode != 0
        finally:
            direct_cluster.stop()
            delegated_cluster.stop()
            policy_only_cluster.stop()
            hba_only_cluster.stop()
            idp.close()
            if previous_ca is None:
                os.environ.pop("SSL_CERT_FILE", None)
            else:
                os.environ["SSL_CERT_FILE"] = previous_ca


def test_reload_separates_trust_policy_cache_entries(integration_environment):
    pg_config, validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-idp-reload-") as directory:
        idp = LocalIdp(pathlib.Path(directory), cache_max_age=120)
        default_identity = (
            "v1." + b64(idp.issuer.encode()) + "." + b64(b"principal")
        )
        uid_identity = "v1." + b64(idp.issuer.encode()) + "." + b64(b"stable-1")
        cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            identity_map=(default_identity, "appuser"), ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        default_token = idp.sign()
        alternate_type_token = idp.sign(token_type="application/at+jwt")
        uid_token = idp.sign(extra_claims={"uid": "stable-1"})
        previous_ca = os.environ.get("SSL_CERT_FILE")
        os.environ["SSL_CERT_FILE"] = str(idp.tls_certificate)

        def connect(token, user="appuser"):
            return attempt_oauth(
                cluster, client, token=token, user=user,
                issuer=idp.issuer, sslmode="require",
            )

        def assert_accepts_after_fresh_fetch(token=default_token, user="appuser"):
            before = idp.request_counts()
            result = connect(token, user=user)
            assert result.returncode == 0, result.stderr + "\n" + cluster.logs()
            after = idp.request_counts()
            assert after["metadata"] > before["metadata"]
            assert after["jwks"] > before["jwks"]

        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")
            psql(cluster, "CREATE ROLE uiduser LOGIN")
            accepted = connect(default_token)
            assert accepted.returncode == 0, accepted.stderr + "\n" + cluster.logs()

            reload_setting(
                cluster, "pg_oauth_validator.allowed_algorithms", "RS256",
            )
            assert_accepts_after_fresh_fetch()

            reload_setting(
                cluster, "pg_oauth_validator.required_token_type",
                "application/at+jwt",
            )
            assert connect(default_token).returncode != 0
            assert_accepts_after_fresh_fetch(alternate_type_token)

            ident_path = cluster.data / "pg_ident.conf"
            ident_path.write_text(
                ident_path.read_text(encoding="utf-8") +
                f"oauthmap {uid_identity} uiduser\n",
                encoding="utf-8",
            )
            reload_setting(cluster, "pg_oauth_validator.identity_claim", "uid")
            psql(cluster, "SELECT pg_reload_conf()")
            before_identity_reload = idp.request_counts()
            assert connect(alternate_type_token).returncode != 0
            after_identity_reload = idp.request_counts()
            assert (after_identity_reload["metadata"] >
                    before_identity_reload["metadata"])
            assert after_identity_reload["jwks"] > before_identity_reload["jwks"]
            uid_alternate_type_token = idp.sign(
                token_type="application/at+jwt",
                extra_claims={"uid": "stable-1"},
            )
            accepted_uid = connect(uid_alternate_type_token, user="uiduser")
            assert accepted_uid.returncode == 0

            reload_setting(
                cluster, "pg_oauth_validator.allowed_jwks_hosts", "localhost",
            )
            assert_accepts_after_fresh_fetch(uid_alternate_type_token, user="uiduser")

            reload_setting(
                cluster, "pg_oauth_validator.allow_insecure_http", "on",
            )
            assert_accepts_after_fresh_fetch(uid_alternate_type_token, user="uiduser")

            reload_setting(
                cluster, "pg_oauth_validator.clock_skew", "30s",
            )
            assert_accepts_after_fresh_fetch(uid_alternate_type_token, user="uiduser")

            reload_setting(
                cluster, "pg_oauth_validator.max_token_size", "4kB",
            )
            assert_accepts_after_fresh_fetch(uid_alternate_type_token, user="uiduser")

            for setting, value in (
                ("metadata_default_ttl", "4min"),
                ("jwks_default_ttl", "4min"),
                ("cache_max_ttl", "30min"),
                ("jwks_stale_grace", "1s"),
                ("unknown_kid_refresh_cooldown", "10s"),
            ):
                reload_setting(
                    cluster, f"pg_oauth_validator.{setting}", value,
                )
                assert_accepts_after_fresh_fetch(
                    uid_alternate_type_token, user="uiduser",
                )

            reload_setting(cluster, "pg_oauth_validator.ca_file", "/missing/ca.pem")
            rejected_bad_ca = connect(uid_alternate_type_token, user="uiduser")
            assert rejected_bad_ca.returncode != 0

            original_ca = str(cluster.data / "oauth-ca.pem")
            reload_setting(cluster, "pg_oauth_validator.ca_file", original_ca)
            restored = connect(uid_alternate_type_token, user="uiduser")
            assert restored.returncode == 0, restored.stderr + "\n" + cluster.logs()

            reload_setting(
                cluster, "pg_oauth_validator.cache_max_entries", "33",
            )
            rejected_capacity_mismatch = connect(
                uid_alternate_type_token, user="uiduser",
            )
            assert rejected_capacity_mismatch.returncode != 0
            reload_setting(
                cluster, "pg_oauth_validator.cache_max_entries", "32",
            )
            restored_capacity = connect(uid_alternate_type_token, user="uiduser")
            assert restored_capacity.returncode == 0

            logs = cluster.logs()
            for secret in (
                default_token, alternate_type_token, uid_token,
                uid_alternate_type_token,
            ):
                assert secret not in logs
                assert secret not in rejected_bad_ca.stderr
                assert secret not in rejected_capacity_mismatch.stderr
        finally:
            cluster.stop()
            idp.close()
            if previous_ca is None:
                os.environ.pop("SSL_CERT_FILE", None)
            else:
                os.environ["SSL_CERT_FILE"] = previous_ca


@pytest.mark.slow
@pytest.mark.xdist_group(name="timing")
def test_local_idp_rotation_outage_and_recovery(integration_environment):
    pg_config, validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-idp-lifecycle-") as directory:
        idp = LocalIdp(pathlib.Path(directory), cache_max_age=3)
        identity = "v1." + b64(idp.issuer.encode()) + "." + b64(b"principal")
        cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            identity_map=(identity, "appuser"), ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        token_a = idp.sign(key_id="integration-key")
        previous_ca = os.environ.get("SSL_CERT_FILE")
        os.environ["SSL_CERT_FILE"] = str(idp.tls_certificate)

        def connect(token):
            return attempt_oauth(
                cluster, client, token=token, user="appuser",
                issuer=idp.issuer, sslmode="require",
            )

        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")

            assert connect(token_a).returncode == 0

            idp.rotate("rotated-key")
            token_b = idp.sign(key_id="rotated-key")
            assert connect(token_b).returncode == 0
            assert connect(token_a).returncode == 0

            idp.retire("integration-key")
            time.sleep(3.2)
            assert connect(token_b).returncode == 0
            retired = connect(token_a)
            assert retired.returncode != 0

            requests_before_outage = idp.request_counts()
            idp.outage = True
            assert connect(token_b).returncode == 0
            assert idp.request_counts() == requests_before_outage

            time.sleep(3.2)
            expired_during_outage = connect(token_b)
            assert expired_during_outage.returncode != 0
            assert (idp.request_counts()["metadata"] >
                    requests_before_outage["metadata"])

            idp.outage = False
            recovered = connect(token_b)
            assert recovered.returncode == 0, recovered.stderr + "\n" + cluster.logs()

            logs = cluster.logs()
            for token in (token_a, token_b):
                assert token not in logs
                assert token not in retired.stderr
                assert token not in expired_during_outage.stderr
        finally:
            cluster.stop()
            idp.close()
            if previous_ca is None:
                os.environ.pop("SSL_CERT_FILE", None)
            else:
                os.environ["SSL_CERT_FILE"] = previous_ca


@pytest.mark.slow
@pytest.mark.xdist_group(name="timing")
def test_local_idp_transport_failures_deny_and_recover(integration_environment):
    pg_config, validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-idp-transport-") as directory:
        idp = LocalIdp(pathlib.Path(directory), cache_max_age=0)
        identity = "v1." + b64(idp.issuer.encode()) + "." + b64(b"principal")
        cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            identity_map=(identity, "appuser"), ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        previous_ca = os.environ.get("SSL_CERT_FILE")
        os.environ["SSL_CERT_FILE"] = str(idp.tls_certificate)

        def connect(token):
            return attempt_oauth(
                cluster, client, token=token, user="appuser",
                issuer=idp.issuer, sslmode="require",
            )

        failure_modes = (
            "http_error", "redirect", "malformed_json", "wrong_content_type",
            "oversized", "truncated", "total_timeout",
        )
        results = []
        tokens = []
        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")
            initial = idp.sign()
            tokens.append(initial)
            assert connect(initial).returncode == 0

            # Stopping the accept loop while retaining the listening socket makes
            # the TLS handshake hit the validator's connection-timeout bound.
            idp.pause_accepting_connections()
            started = time.monotonic()
            connection_timeout = connect(initial)
            elapsed = time.monotonic() - started
            results.append(connection_timeout)
            assert connection_timeout.returncode != 0
            assert elapsed < 4.0, "connection timeout exceeded its configured bound"
            idp.resume_accepting_connections()
            assert connect(initial).returncode == 0

            for resource in ("metadata", "jwks"):
                for index, fault in enumerate(failure_modes):
                    idp.rotate(f"transport-{resource}-{index}")
                    token = idp.sign()
                    tokens.append(token)
                    idp.set_fault(resource, fault)
                    started = time.monotonic()
                    rejected = connect(token)
                    elapsed = time.monotonic() - started
                    results.append(rejected)
                    assert rejected.returncode != 0, f"{resource} {fault} accepted"
                    assert elapsed < 8.0, f"{resource} {fault} was not bounded"

                    idp.set_fault(resource, None)
                    recovered = connect(token)
                    assert recovered.returncode == 0, (
                        f"{resource} did not recover after {fault}: "
                        f"{recovered.stderr}\n{cluster.logs()}"
                    )

            logs = cluster.logs()
            assert IDP_RESPONSE_SECRET not in logs
            for token in tokens:
                assert token not in logs
            for result in results:
                assert IDP_RESPONSE_SECRET not in result.stdout
                assert IDP_RESPONSE_SECRET not in result.stderr
                for token in tokens:
                    assert token not in result.stdout
                    assert token not in result.stderr
        finally:
            cluster.stop()
            idp.close()
            if previous_ca is None:
                os.environ.pop("SSL_CERT_FILE", None)
            else:
                os.environ["SSL_CERT_FILE"] = previous_ca


@pytest.mark.slow
@pytest.mark.xdist_group(name="timing")
def test_unknown_kid_refresh_is_suppressed_across_backends(
        integration_environment):
    pg_config, validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-idp-unknown-kid-") as directory:
        idp = LocalIdp(pathlib.Path(directory), cache_max_age=120)
        identity = "v1." + b64(idp.issuer.encode()) + "." + b64(b"principal")
        cluster = TemporaryPostgres(
            pg_config, validator_library, issuer=idp.issuer,
            identity_map=(identity, "appuser"), ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate, server_key=idp.tls_key,
        )
        valid = idp.sign()
        unknown_tokens = [
            idp.sign(header_key_id=f"attacker-key-{index}")
            for index in range(8)
        ]
        after_cooldown = idp.sign(header_key_id="attacker-key-after-cooldown")
        previous_ca = os.environ.get("SSL_CERT_FILE")
        os.environ["SSL_CERT_FILE"] = str(idp.tls_certificate)

        def connect(token):
            return attempt_oauth(
                cluster, client, token=token, user="appuser",
                issuer=idp.issuer, sslmode="require",
            )

        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")
            assert connect(valid).returncode == 0
            warmed = idp.request_counts()

            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                rejected = list(pool.map(connect, unknown_tokens))
            assert all(result.returncode != 0 for result in rejected)
            after_attack = idp.request_counts()
            assert after_attack["jwks"] == warmed["jwks"] + 1

            assert connect(valid).returncode == 0
            assert idp.request_counts() == after_attack

            time.sleep(30.2)
            assert connect(after_cooldown).returncode != 0
            assert idp.request_counts()["jwks"] == after_attack["jwks"] + 1

            logs = cluster.logs()
            for token in [valid, after_cooldown, *unknown_tokens]:
                assert token not in logs
            for result in rejected:
                for token in unknown_tokens:
                    assert token not in result.stderr
        finally:
            cluster.stop()
            idp.close()
            if previous_ca is None:
                os.environ.pop("SSL_CERT_FILE", None)
            else:
                os.environ["SSL_CERT_FILE"] = previous_ca


@pytest.mark.xdist_group(name="timing")
def test_shared_cache_cross_backend_refresh_suppression(integration_environment):
    pg_config, validator_library, _client = integration_environment
    cache_probe = pathlib.Path(os.environ["CACHE_PROBE"]).resolve()
    cluster = TemporaryPostgres(pg_config, validator_library)
    key = "concurrency-" + uuid.uuid4().hex
    payload = "validated-public-resource"

    try:
        cluster.start()
        library = str(cache_probe).replace("'", "''")
        psql(cluster, f"""
            CREATE FUNCTION pg_oauth_test_cache_refresh(text, bigint, integer, text)
            RETURNS integer AS '{library}', 'pg_oauth_test_cache_refresh'
            LANGUAGE C STRICT;
            CREATE FUNCTION pg_oauth_test_cache_begin(text, bigint)
            RETURNS integer AS '{library}', 'pg_oauth_test_cache_begin'
            LANGUAGE C STRICT;
            CREATE FUNCTION pg_oauth_test_cache_lookup(text, bigint)
            RETURNS text AS '{library}', 'pg_oauth_test_cache_lookup'
            LANGUAGE C STRICT;
        """)

        owner_sql = (
            "SELECT pg_oauth_test_cache_refresh("
            f"'{key}', 1000, 3000, '{payload}')"
        )
        owner = subprocess.Popen(
            [str(value) for value in psql_argv(cluster, owner_sql)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        time.sleep(0.5)

        started = time.monotonic()
        cold_lookup = psql(
            cluster,
            f"SELECT pg_oauth_test_cache_lookup('{key}', 1001) IS NULL",
        )
        competitor = psql(
            cluster, f"SELECT pg_oauth_test_cache_begin('{key}', 1001)"
        )
        elapsed = time.monotonic() - started
        owner_stdout, owner_stderr = owner.communicate(timeout=5)

        assert cold_lookup.stdout.strip() == "t"
        assert competitor.stdout.strip() == "1"  # refresh in progress
        assert elapsed < 1.5, "competitor waited on refresh/network work"
        assert owner.returncode == 0, owner_stderr
        assert owner_stdout.strip() == "0"  # refresh owner
        published = psql(
            cluster, f"SELECT pg_oauth_test_cache_lookup('{key}', 1002)"
        )
        assert published.stdout.strip() == payload
    finally:
        cluster.stop()
