"""Keep the independently configured demo trust boundaries aligned."""

import json
import pathlib
import re
import shlex


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEMO = ROOT / "demo"


def setting(name):
    config = (DEMO / "postgresql.conf").read_text(encoding="utf-8")
    match = re.search(rf"^{re.escape(name)}\s*=\s*'([^']+)'\s*$", config, re.M)
    assert match is not None, name
    return match.group(1)


def test_demo_configuration():
    realm = json.loads((DEMO / "keycloak-realm.json").read_text(encoding="utf-8"))
    client = next(c for c in realm["clients"] if c["clientId"] == "postgres-demo")
    scope = next(s for s in realm["clientScopes"] if s["name"] == "connect:postgres")
    audience = next(
        m["config"]["included.custom.audience"]
        for m in client["protocolMappers"]
        if m["protocolMapper"] == "oidc-audience-mapper"
    )
    hba = next(
        shlex.split(line)
        for line in (DEMO / "pg_hba.conf").read_text(encoding="utf-8").splitlines()
        if line.startswith("hostssl ")
    )
    hba_options = dict(option.split("=", 1) for option in hba[5:])
    psql_script = (DEMO / "psql.sh").read_text(encoding="utf-8")
    conninfo = re.search(r'"(host=localhost [^"\n]+)"', psql_script)
    assert conninfo is not None
    client_options = dict(option.split("=", 1) for option in conninfo.group(1).split())

    issuer = "https://localhost:8443/realms/" + realm["realm"]
    assert hba[1:5] == ["demo", "app_reader", "all", "oauth"]
    assert hba_options == {
        "issuer": issuer,
        "scope": scope["name"],
        "validator": "pg_oauth_validator",
        "map": "demo",
    }
    assert client_options["oauth_issuer"] == issuer
    assert client_options["oauth_client_id"] == client["clientId"]
    assert client_options["oauth_scope"] == scope["name"]
    assert client_options["sslmode"] == "verify-full"
    assert client["publicClient"] is True
    assert client["consentRequired"] is True
    assert client["attributes"]["access.token.header.type.rfc9068"] == "true"
    assert client["attributes"]["oauth2.device.authorization.grant.enabled"] == "true"
    assert scope["name"] in client["optionalClientScopes"]
    assert scope["attributes"]["include.in.token.scope"] == "true"
    assert scope["attributes"]["display.on.consent.screen"] == "true"
    assert audience == setting("pg_oauth_validator.audiences")
    assert setting("pg_oauth_validator.ca_file") == "/certs/ca.crt"
    assert any(m["protocolMapper"] == "oidc-sub-mapper" for m in client["protocolMappers"])

    alice = next(user for user in realm["users"] if user["username"] == "alice")
    identity_map = (DEMO / "pg_ident.conf").read_text(encoding="utf-8").splitlines()
    assert f"demo {alice['id']} app_reader" in identity_map
    assert "CREATE ROLE app_reader LOGIN;" in (DEMO / "init.sql").read_text(encoding="utf-8")


if __name__ == "__main__":
    test_demo_configuration()
