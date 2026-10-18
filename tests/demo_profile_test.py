"""The external Compose profile must render aligned, fail-closed configuration."""

import importlib.util
import pathlib
import re
import subprocess
import sys

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "demo" / "render-profile.py"
EXAMPLE = ROOT / "demo" / "provider.example.yml"
WORKOS_EXAMPLE = ROOT / "demo" / "providers" / "workos.example.yml"
SPEC = importlib.util.spec_from_file_location("demo_profile", SCRIPT)
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)


def test_example_renders_aligned_server_and_client(tmp_path):
    config = PROFILE.load_profile(EXAMPLE)
    PROFILE.render(config, tmp_path)
    server = (tmp_path / "postgresql.conf").read_text()
    hba = (tmp_path / "pg_hba.conf").read_text()
    ident = (tmp_path / "pg_ident.conf").read_text()
    client = (tmp_path / "pg_service.conf").read_text()
    assert f"pg_oauth_validator.audiences = '{config['audience']}'" in server
    assert f"pg_oauth_validator.identity_claim = '{config['identity_claim']}'" in server
    assert "pg_oauth_validator.role_claim = ''" in server
    assert "pg_oauth_validator.allowed_token_types = 'at+jwt,application/at+jwt'" in server
    assert "pg_oauth_validator.allow_missing_token_type = off" in server
    assert f'issuer="{config["issuer"]}"' in hba
    assert f'scope="{config["scope"]}"' in hba
    assert f"external {config['identity_value']} app_reader" in ident
    assert f"oauth_issuer={config['issuer']}" in client
    assert f"oauth_client_id={config['client_id']}" in client
    assert f"oauth_scope={config['scope']}" in client
    assert "sslmode=verify-full" in client
    assert "hostssl demo app_reader all oauth" in hba
    assert "map=external" in hba
    assert "host all all all reject" in hba


def test_workos_example_renders_delegated_missing_type_policy(tmp_path):
    profile = PROFILE.load_profile(WORKOS_EXAMPLE)
    PROFILE.render(profile, tmp_path)
    server = (tmp_path / "postgresql.conf").read_text()
    hba = (tmp_path / "pg_hba.conf").read_text()
    client = (tmp_path / "pg_service.conf").read_text()
    assert "pg_oauth_validator.role_claim = 'role'" in server
    assert "pg_oauth_validator.allow_missing_token_type = on" in server
    assert "pg_oauth_validator.allowed_token_types = 'at+jwt,application/at+jwt'" in server
    assert "delegate_ident_mapping=1" in hba
    assert "map=external" not in hba
    assert "oauth_client_id=client_REPLACE_WITH_PUBLIC_DEVICE_CLIENT_ID" in client
    assert "oauth_scope=connect:postgres" in client
    assert "replace-with-stable-subject" not in server + hba + client


@pytest.mark.parametrize(
    "change,code",
    [
        ("issuer: https://other.example/\n", "profile_key"),
        ("audience: [wrong]\n", "profile_value"),
        ("audience: postgres-one,postgres-two\n", "profile_audience"),
        ("scope: openid\n", "profile_scope"),
        ("scope: connect:postgres other\n", "profile_scope"),
        ("issuer: http://issuer.example/\n", "profile_issuer"),
        ("issuer: https://user@issuer.example/\n", "profile_issuer"),
        ("issuer: https://issuer.example/?x=y\n", "profile_issuer"),
        ("identity_value: /regex\n", "profile_identity_value"),
        ("identity_value: 'a\\nrole'\n", "profile_identity_value"),
        ("unexpected: value\n", "profile_key"),
        ("allowed_token_types: 'JWT'\n", "profile_key"),
        ("scope: &shared connect:postgres\n", "profile_yaml_feature"),
        ("scope: *shared\n", "profile_yaml_feature"),
    ],
)
def test_rejects_bad_profile_without_output(tmp_path, change, code):
    source = EXAMPLE.read_text()
    key = change.split(":", 1)[0]
    if key in ("issuer", "audience", "scope", "identity_value") and code != "profile_key":
        source = re.sub(rf"^{key}:.*$", change.rstrip("\n"), source, flags=re.M)
    else:
        source += change
    profile = tmp_path / "profile.yml"
    output = tmp_path / "output"
    profile.write_text(source)
    result = subprocess.run(
        [sys.executable, str(SCRIPT), str(profile), str(output)],
        capture_output=True, text=True, check=False,
    )
    assert result.returncode == 1
    assert result.stderr.strip() == f"profile_error: {code}"
    assert not output.exists()


def test_missing_profile_and_oversize_fail_closed(tmp_path):
    with pytest.raises(PROFILE.ProfileError, match="profile_unavailable"):
        PROFILE.load_profile(tmp_path / "absent.yml")
    path = tmp_path / "oversize.yml"
    path.write_bytes(b"a" * 32769)
    with pytest.raises(PROFILE.ProfileError, match="profile_size"):
        PROFILE.load_profile(path)


@pytest.mark.parametrize("token_types", ["", "JWT", "at+jwt,JWT", "at+jwt,at+jwt",
                                           "at+jwt,", "at+jwt,,application/at+jwt"])
def test_rejects_invalid_token_type_list(tmp_path, token_types):
    path = tmp_path / "profile.yml"
    path.write_text(EXAMPLE.read_text().replace(
        'allowed_token_types: "at+jwt,application/at+jwt"',
        f'allowed_token_types: "{token_types}"'))
    with pytest.raises(PROFILE.ProfileError, match="^profile_token_type$"):
        PROFILE.load_profile(path)


def test_missing_type_option_is_explicit_and_boolean(tmp_path):
    path = tmp_path / "profile.yml"
    source = EXAMPLE.read_text()
    path.write_text(source.replace("allow_missing_token_type: false",
                                   "allow_missing_token_type: true"))
    profile = PROFILE.load_profile(path)
    PROFILE.render(profile, tmp_path / "output")
    assert "pg_oauth_validator.allow_missing_token_type = on" in (
        tmp_path / "output" / "postgresql.conf").read_text()
    path.write_text(source.replace("allow_missing_token_type: false",
                                   "allow_missing_token_type: 'true'"))
    with pytest.raises(PROFILE.ProfileError, match="^profile_token_type$"):
        PROFILE.load_profile(path)


def test_token_type_list_is_canonicalized_before_render(tmp_path):
    path = tmp_path / "profile.yml"
    path.write_text(EXAMPLE.read_text().replace(
        'allowed_token_types: "at+jwt,application/at+jwt"',
        'allowed_token_types: "at+jwt, application/at+jwt\\n"'))
    profile = PROFILE.load_profile(path)
    PROFILE.render(profile, tmp_path / "output")
    assert "pg_oauth_validator.allowed_token_types = 'at+jwt,application/at+jwt'\n" in (
        tmp_path / "output" / "postgresql.conf").read_text()


@pytest.mark.parametrize(
    "edit,code",
    [
        (lambda s: s.replace('client_id: "postgres-public-client"\n', ""), "profile_missing"),
        (lambda s: s.replace('identity_claim: "sub"', "identity_claim: 17"), "profile_value"),
        (lambda s: s.replace('identity_value: "replace-with-stable-subject"', "identity_value: " + "a" * 64), "profile_identity_value"),
        (lambda s: s + "ca_pem: 'not a certificate'\n", "profile_ca"),
        (lambda s: s.replace('audience: "https://postgres.example/"', "audience: [bad"), "profile_yaml"),
    ],
)
def test_other_invalid_shapes(tmp_path, edit, code):
    path = tmp_path / "profile.yml"
    path.write_text(edit(EXAMPLE.read_text()))
    with pytest.raises(PROFILE.ProfileError, match=f"^{code}$"):
        PROFILE.load_profile(path)


def test_auth0_style_subject_is_literal(tmp_path):
    source = EXAMPLE.read_text().replace(
        "replace-with-stable-subject", "auth0|stable-123"
    )
    profile = tmp_path / "profile.yml"
    profile.write_text(source)
    config = PROFILE.load_profile(profile)
    PROFILE.render(config, tmp_path / "output")
    assert (tmp_path / "output" / "pg_ident.conf").read_text() == (
        "external auth0|stable-123 app_reader\n"
    )


def test_role_claim_renders_delegated_authorization(tmp_path):
    source = EXAMPLE.read_text().replace(
        'identity_value: "replace-with-stable-subject"\n', ""
    ) + 'role_claim: "postgres_roles"\n'
    path = tmp_path / "profile.yml"
    path.write_text(source)
    profile = PROFILE.load_profile(path)
    PROFILE.render(profile, tmp_path / "output")
    server = (tmp_path / "output" / "postgresql.conf").read_text()
    hba = (tmp_path / "output" / "pg_hba.conf").read_text()
    ident = (tmp_path / "output" / "pg_ident.conf").read_text()
    assert "pg_oauth_validator.role_claim = 'postgres_roles'" in server
    assert "pg_oauth_validator.allowed_token_types = 'at+jwt,application/at+jwt'" in server
    assert "delegate_ident_mapping=1" in hba
    assert "map=external" not in hba
    assert "external " not in ident


@pytest.mark.parametrize("edit,code", [
    (lambda s: s + 'role_claim: "postgres_roles"\n', "profile_mapping"),
    (lambda s: s.replace('identity_value: "replace-with-stable-subject"\n', ""),
     "profile_mapping"),
    (lambda s: s.replace('identity_value: "replace-with-stable-subject"\n', "") +
     'role_claim: "bad claim"\n', "profile_role_claim"),
])
def test_role_claim_configuration_fails_closed(tmp_path, edit, code):
    path = tmp_path / "profile.yml"
    path.write_text(edit(EXAMPLE.read_text()))
    with pytest.raises(PROFILE.ProfileError, match=f"^{code}$"):
        PROFILE.load_profile(path)


def test_private_ca_is_validated_and_rendered(tmp_path):
    cert = tmp_path / "issuer.crt"
    key = tmp_path / "issuer.key"
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
         "-days", "1", "-subj", "/CN=issuer.example", "-keyout", str(key),
         "-out", str(cert)],
        capture_output=True, check=True,
    )
    profile = tmp_path / "profile.yml"
    profile.write_text(
        EXAMPLE.read_text() + "ca_pem: |\n" +
        "".join("  " + line for line in cert.read_text().splitlines(keepends=True))
    )
    config = PROFILE.load_profile(profile)
    output = tmp_path / "output"
    PROFILE.render(config, output)
    assert (output / "provider-ca.crt").read_text() == cert.read_text()
    assert "pg_oauth_validator.ca_file = '/demo/runtime/provider-ca.crt'" in (
        output / "postgresql.conf"
    ).read_text()
