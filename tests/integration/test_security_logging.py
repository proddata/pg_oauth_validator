import os
import pathlib
import tempfile

import pytest

from integration_support import (
    IDP_RESPONSE_SECRET,
    LocalIdp,
    TemporaryPostgres,
    attempt_oauth,
    b64,
    psql,
)


TOKEN_SECRET = "token-secret-sentinel-never-log"
SIGNATURE_SECRET = "signature-secret-sentinel-never-log"
CLAIM_SECRET = "claim-secret-sentinel-never-log"


@pytest.fixture
def integration_environment():
    return (
        os.environ["PG_CONFIG"],
        os.environ["VALIDATOR_LIBRARY"],
        pathlib.Path(os.environ["OAUTH_TEST_CLIENT"]).resolve(),
    )


def assert_protected_values_absent(values, results, logs):
    for value in values:
        assert value not in logs
        for result in results:
            assert value not in result.stdout
            assert value not in result.stderr


def test_diagnostics_redact_token_signature_claim_and_response(
        integration_environment):
    pg_config, validator_library, client = integration_environment
    with tempfile.TemporaryDirectory(prefix="pg-oauth-redaction-") as directory:
        idp = LocalIdp(pathlib.Path(directory), cache_max_age=120)
        identity = "v1." + b64(idp.issuer.encode()) + "." + b64(b"principal")
        cluster = TemporaryPostgres(
            pg_config,
            validator_library,
            issuer=idp.issuer,
            identity_map=(identity, "appuser"),
            ca_file=idp.tls_certificate,
            server_certificate=idp.tls_certificate,
            server_key=idp.tls_key,
        )
        previous_ca = os.environ.get("SSL_CERT_FILE")
        os.environ["SSL_CERT_FILE"] = str(idp.tls_certificate)
        results = []

        def connect(token):
            result = attempt_oauth(
                cluster,
                client,
                token=token,
                user="appuser",
                issuer=idp.issuer,
                sslmode="require",
            )
            results.append(result)
            return result

        try:
            cluster.start()
            psql(cluster, "CREATE ROLE appuser LOGIN")

            # A successful request exercises normal connection logging with a
            # sensitive private claim present in the verified payload.
            accepted = idp.sign(extra_claims={"private_data": CLAIM_SECRET})
            assert connect(accepted).returncode == 0

            # Use a literal sentinel as the complete hostile token so a log of
            # raw callback input is detected without relying on JWT encoding.
            assert connect(TOKEN_SECRET).returncode != 0

            # The signature segment is already Base64URL-safe, so the exact
            # signature bytes presented to the validator are the sentinel.
            signed = idp.sign()
            invalid_signature = signed.rsplit(".", 1)[0] + "." + SIGNATURE_SECRET
            assert connect(invalid_signature).returncode != 0

            wrong_claim = idp.sign(
                audience="https://wrong-resource.example/",
                extra_claims={"private_data": CLAIM_SECRET},
            )
            assert connect(wrong_claim).returncode != 0

            # Force a JWKS fetch so an HTTP error body containing the fixture's
            # response sentinel reaches the transport boundary.
            idp.rotate("redaction-response-key")
            idp.set_fault("jwks", "http_error")
            assert connect(idp.sign()).returncode != 0
            idp.set_fault("jwks", None)

            logs = cluster.logs()
            protected = (
                TOKEN_SECRET,
                SIGNATURE_SECRET,
                CLAIM_SECRET,
                IDP_RESPONSE_SECRET,
                accepted,
                signed,
                invalid_signature,
                wrong_claim,
            )
            assert_protected_values_absent(protected, results, logs)

            # Protected server diagnostics remain useful without reproducing
            # hostile bytes. These codes cover parsing, crypto, claims, and I/O.
            assert "validator=validator_envelope" in logs
            assert "validator=validator_signature" in logs
            assert "validator=validator_claims" in logs
            assert "validator=validator_issuer_key" in logs
            assert "OAuth access token rejected" in logs
        finally:
            cluster.stop()
            idp.close()
            if previous_ca is None:
                os.environ.pop("SSL_CERT_FILE", None)
            else:
                os.environ["SSL_CERT_FILE"] = previous_ca
