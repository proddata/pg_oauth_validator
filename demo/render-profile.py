#!/usr/bin/env python3
"""Render the external Compose example from one deliberately narrow YAML profile."""

import pathlib
import re
import ssl
import sys
from urllib.parse import urlsplit

import yaml


REQUIRED = frozenset({"issuer", "audience", "scope", "client_id", "identity_claim"})
OPTIONAL = frozenset({"identity_value", "role_claim", "ca_pem",
                      "allowed_token_types", "allow_missing_token_type"})
DEFAULT_TOKEN_TYPES = "at+jwt,application/at+jwt"
ACCESS_TOKEN_TYPES = frozenset({"at+jwt", "application/at+jwt"})
SAFE_VALUE = re.compile(r"\A[A-Za-z0-9][A-Za-z0-9._~:/?&=%+@|-]*\Z", re.ASCII)
SAFE_TOKEN = re.compile(r"\A[A-Za-z0-9][A-Za-z0-9._:/-]*\Z", re.ASCII)
SAFE_IDENTITY = re.compile(r"\A[A-Za-z0-9][A-Za-z0-9._:@|/-]*\Z", re.ASCII)
SAFE_CLAIM = re.compile(r"\A[A-Za-z_][A-Za-z0-9_]*\Z", re.ASCII)
PEM = re.compile(
    r"\A-----BEGIN CERTIFICATE-----\n(?:[A-Za-z0-9+/=]{1,64}\n)+"
    r"-----END CERTIFICATE-----\n?\Z"
)


class ProfileError(Exception):
    """A safe, non-sensitive configuration diagnostic."""


def load_profile(path):
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise ProfileError("profile_unavailable") from exc
    if not raw or len(raw) > 32768:
        raise ProfileError("profile_size")
    try:
        source = raw.decode("utf-8")
        if any(
            isinstance(token, (yaml.tokens.AnchorToken, yaml.tokens.AliasToken,
                               yaml.tokens.TagToken, yaml.tokens.DirectiveToken))
            for token in yaml.scan(source)
        ):
            raise ProfileError("profile_yaml_feature")
        root = yaml.compose(source, Loader=yaml.SafeLoader)
    except (UnicodeError, yaml.YAMLError) as exc:
        raise ProfileError("profile_yaml") from exc
    if not isinstance(root, yaml.MappingNode):
        raise ProfileError("profile_shape")
    values = {}
    for key, value in root.value:
        if not isinstance(key, yaml.ScalarNode) or key.tag != "tag:yaml.org,2002:str":
            raise ProfileError("profile_key")
        name = key.value
        if name not in REQUIRED | OPTIONAL or name in values:
            raise ProfileError("profile_key")
        if not isinstance(value, yaml.ScalarNode):
            raise ProfileError("profile_value")
        if name == "allow_missing_token_type":
            if value.tag != "tag:yaml.org,2002:bool" or value.value not in ("true", "false"):
                raise ProfileError("profile_token_type")
            values[name] = value.value == "true"
            continue
        if value.tag != "tag:yaml.org,2002:str":
            raise ProfileError("profile_value")
        values[name] = value.value
    if not REQUIRED.issubset(values):
        raise ProfileError("profile_missing")
    if ("role_claim" in values) == ("identity_value" in values):
        raise ProfileError("profile_mapping")
    for name in REQUIRED:
        value = values[name]
        if not value or len(value.encode("utf-8")) > 1024:
            raise ProfileError("profile_value")
    values.setdefault("allowed_token_types", DEFAULT_TOKEN_TYPES)
    values.setdefault("allow_missing_token_type", False)
    types = [item.strip() for item in values["allowed_token_types"].split(",")]
    if (not types or len(types) != len(set(types)) or
            any(item not in ACCESS_TOKEN_TYPES for item in types)):
        raise ProfileError("profile_token_type")
    values["allowed_token_types"] = ",".join(types)
    issuer = values["issuer"]
    if not SAFE_VALUE.fullmatch(issuer):
        raise ProfileError("profile_issuer")
    parsed = urlsplit(issuer)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username or
            parsed.password or parsed.query or parsed.fragment or
            not re.fullmatch(r"[A-Za-z0-9.-]+", parsed.hostname, re.ASCII)):
        raise ProfileError("profile_issuer")
    try:
        port = parsed.port
    except ValueError as exc:
        raise ProfileError("profile_issuer") from exc
    if port is not None and not 1 <= port <= 65535:
        raise ProfileError("profile_issuer")
    if not SAFE_VALUE.fullmatch(values["audience"]):
        raise ProfileError("profile_audience")
    if (not SAFE_TOKEN.fullmatch(values["scope"]) or
            values["scope"] == "openid"):
        raise ProfileError("profile_scope")
    if not SAFE_TOKEN.fullmatch(values["client_id"]):
        raise ProfileError("profile_client_id")
    if not SAFE_CLAIM.fullmatch(values["identity_claim"]):
        raise ProfileError("profile_identity_claim")
    if "identity_value" in values:
        if (len(values["identity_value"].encode("utf-8")) > 63 or
                not SAFE_IDENTITY.fullmatch(values["identity_value"])):
            raise ProfileError("profile_identity_value")
    if "role_claim" in values:
        if (len(values["role_claim"]) > 128 or
                not SAFE_CLAIM.fullmatch(values["role_claim"])):
            raise ProfileError("profile_role_claim")
    if "ca_pem" in values:
        cert = values["ca_pem"]
        if len(cert) > 16384 or not PEM.fullmatch(cert):
            raise ProfileError("profile_ca")
        try:
            ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT).load_verify_locations(cadata=cert)
        except (ValueError, ssl.SSLError) as exc:
            raise ProfileError("profile_ca") from exc
    return values


def render(profile, output):
    """Write only fixed templates after all profile values have passed validation."""
    output.mkdir(mode=0o755, parents=True, exist_ok=True)
    ca_path = "/demo/runtime/provider-ca.crt" if "ca_pem" in profile else ""
    delegated = "role_claim" in profile
    role_claim = profile.get("role_claim", "")
    files = {
        "postgresql.conf": (
            "listen_addresses = '*'\n"
            "port = 5432\n"
            "hba_file = '/demo/runtime/pg_hba.conf'\n"
            "ident_file = '/demo/runtime/pg_ident.conf'\n"
            "oauth_validator_libraries = 'pg_oauth_validator'\n"
            f"pg_oauth_validator.audiences = '{profile['audience']}'\n"
            f"pg_oauth_validator.identity_claim = '{profile['identity_claim']}'\n"
            f"pg_oauth_validator.role_claim = '{role_claim}'\n"
            f"pg_oauth_validator.allowed_token_types = '{profile['allowed_token_types']}'\n"
            "pg_oauth_validator.allow_missing_token_type = "
            f"{'on' if profile['allow_missing_token_type'] else 'off'}\n"
            f"pg_oauth_validator.ca_file = '{ca_path}'\n"
            "ssl = on\n"
            "ssl_cert_file = '/certs/postgres.crt'\n"
            "ssl_key_file = '/certs/postgres.key'\n"
            "log_connections = on\n"
        ),
        "pg_hba.conf": (
            "local all postgres peer\n"
            "local all all reject\n"
            f"hostssl demo app_reader all oauth issuer=\"{profile['issuer']}\" "
            f"scope=\"{profile['scope']}\" validator=pg_oauth_validator "
            f"{'delegate_ident_mapping=1' if delegated else 'map=external'}\n"
            "host all all all reject\n"
        ),
        "pg_ident.conf": ("# delegated role authorization; no identity map\n"
                          if delegated else
                          f"external {profile['identity_value']} app_reader\n"),
        "init.sql": "CREATE ROLE app_reader LOGIN;\nGRANT CONNECT ON DATABASE demo TO app_reader;\n",
        "pg_service.conf": (
            "[external]\n"
            "host=localhost\n"
            "dbname=demo\n"
            "user=app_reader\n"
            "sslmode=verify-full\n"
            "sslrootcert=/certs/ca.crt\n"
            f"oauth_issuer={profile['issuer']}\n"
            f"oauth_client_id={profile['client_id']}\n"
            f"oauth_scope={profile['scope']}\n"
        ),
    }
    if "ca_pem" in profile:
        files["provider-ca.crt"] = profile["ca_pem"]
    for name, contents in files.items():
        (output / name).write_text(contents, encoding="utf-8")


def main():
    if len(sys.argv) != 3:
        print("profile_error: usage", file=sys.stderr)
        return 2
    try:
        profile = load_profile(pathlib.Path(sys.argv[1]))
        render(profile, pathlib.Path(sys.argv[2]))
    except ProfileError as exc:
        print(f"profile_error: {exc}", file=sys.stderr)
        return 1
    except OSError:
        print("profile_error: output_unavailable", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
