#!/usr/bin/env python3
"""Create and run named Auth0 interoperability playground profiles."""

import argparse
import base64
import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
EXAMPLES = ROOT / "providers" / "auth0" / "examples"
PROFILES = Path(os.environ.get("PLAYGROUND_PROFILES_DIR", ROOT / ".generated" / "auth0"))
MODES = ("direct", "issuer-qualified", "claim-roles")
CLIENT_MODES = ("node", "psql", "both")
ROLE_RE = re.compile(r"[a-z_][a-z0-9_]{0,62}\Z")
NAME_RE = re.compile(r"[a-z0-9][a-z0-9_-]{0,62}\Z")


def prompt(label, default=None):
    suffix = f" [{default}]" if default is not None else ""
    value = input(f"{label}{suffix}: ").strip()
    if value:
        return value
    if default is not None:
        return default
    raise ValueError(f"{label} is required")


def prompt_choice(label, choices, default=0):
    print(f"{label}:")
    for index, (value, description) in enumerate(choices, start=1):
        print(f"  {index}) {description}")
    selected = input(f"Select [{default + 1}]: ").strip()
    if not selected:
        return choices[default][0]
    if selected.isdigit() and 1 <= int(selected) <= len(choices):
        return choices[int(selected) - 1][0]
    raise ValueError(f"select a number from 1 to {len(choices)}")


def safe_config_value(name, value):
    if not value or any(
        character.isspace() or character in "'\"\\" for character in value
    ):
        raise ValueError(f"{name} must not be empty or contain quotes, backslashes, or whitespace")
    return value


def parse_roles(value):
    roles = [role.strip() for role in value.split(",")]
    if not roles or any(not ROLE_RE.fullmatch(role) for role in roles):
        raise ValueError("roles must be comma-separated, unquoted PostgreSQL role names")
    if len(set(roles)) != len(roles):
        raise ValueError("roles must not contain duplicates")
    return roles


def base64url(value):
    return base64.urlsafe_b64encode(value.encode()).decode().rstrip("=")


def render(template, replacements):
    rendered = template.read_text(encoding="utf-8")
    for key, value in replacements.items():
        rendered = rendered.replace(f"@{key}@", value)
    unresolved = sorted(set(re.findall(r"@[A-Z][A-Z0-9_]*@", rendered)))
    if unresolved:
        raise ValueError(f"unresolved template values: {', '.join(unresolved)}")
    return rendered


def create(args):
    if not NAME_RE.fullmatch(args.name):
        raise ValueError("profile name must use lowercase letters, digits, '-' or '_'")
    mode = args.mode or prompt_choice(
        "Identity and authorization mode",
        (
            ("direct", "Direct claim-to-role matching"),
            ("issuer-qualified", "Issuer-qualified identity through pg_ident.conf"),
            ("claim-roles", "Delegated authorization using a roles claim"),
        ),
    )
    if mode not in MODES:
        raise ValueError(f"mapping mode must be one of: {', '.join(MODES)}")

    issuer = safe_config_value("issuer", args.issuer or prompt("Auth0 issuer (include trailing /)"))
    if not issuer.startswith("https://") or not issuer.endswith("/"):
        raise ValueError("Auth0 issuer must be an HTTPS URL ending in '/'")
    discovery_uri = safe_config_value(
        "discovery URI",
        args.discovery_uri
        or prompt("OpenID discovery URL", issuer + ".well-known/openid-configuration"),
    )
    if not discovery_uri.startswith("https://"):
        raise ValueError("discovery URI must be HTTPS")
    client_id = safe_config_value("client ID", args.client_id or prompt("Auth0 Native application client ID"))
    audience = safe_config_value("audience", args.audience or prompt("Auth0 API identifier"))
    scope = safe_config_value("scope", args.scope or prompt("Required scope", "connect:postgres"))
    roles = parse_roles(args.roles or prompt("PostgreSQL login roles", "app_reader,app_writer"))
    postgres_role = args.role or prompt_choice(
        "PostgreSQL role used for the connection",
        tuple((role, role) for role in roles),
    )
    if postgres_role not in roles:
        raise ValueError("connection role must be one of the configured PostgreSQL login roles")
    port = args.port or int(prompt("Host PostgreSQL port", str(55432 + len(profiles()))))
    if port < 1 or port > 65535:
        raise ValueError("host PostgreSQL port must be between 1 and 65535")
    client_mode = args.client or prompt_choice(
        "Clients to test",
        (
            ("node", "Node.js OAuth client"),
            ("psql", "Stock psql/libpq client"),
            ("both", "Both Node.js and psql"),
        ),
    )

    subject = None
    identity_claim = "sub"
    roles_claim = "roles"
    if mode == "issuer-qualified":
        subject = safe_config_value("subject", args.subject or prompt("Auth0 test user's exact sub"))
    elif mode == "direct":
        identity_claim = safe_config_value(
            "identity claim",
            args.identity_claim
            or prompt("Scalar Auth0 claim containing the requested PostgreSQL role", "https://company.example/postgres_role"),
        )
    else:
        roles_claim = safe_config_value(
            "roles claim",
            args.roles_claim
            or prompt("Array Auth0 claim containing allowed PostgreSQL roles", "https://company.example/postgres_roles"),
        )

    profile = PROFILES / args.name
    if profile.exists() and not args.force:
        raise ValueError(f"profile already exists: {profile} (use --force to replace it)")
    profile.mkdir(parents=True, exist_ok=True)

    identity = ""
    if subject is not None:
        identity = f"v1.{base64url(issuer)}.{base64url(subject)}"
    replacements = {
        "OAUTH_ISSUER": issuer,
        "OAUTH_AUDIENCE": audience,
        "OAUTH_SCOPE": scope,
        "OAUTH_IDENTITY": identity,
        "DATABASE_ROLES": ",".join(roles),
        "IDENTITY_CLAIM": identity_claim,
        "ROLES_CLAIM": roles_claim,
        "IDENT_MAP_ROWS": "\n".join(f"oauthmap  {identity}  {role}" for role in roles),
        "CREATE_ROLES": "\n".join(f"CREATE ROLE {role} LOGIN;" for role in roles),
        "GRANT_ROLES": ", ".join(roles),
    }
    source = EXAMPLES / mode
    files = {
        "postgresql.conf": "postgresql.conf.template",
        "pg_hba.conf": "pg_hba.conf.template",
        "pg_hba.pg19.conf": "pg_hba.pg19.conf.template",
        "pg_ident.conf": "pg_ident.conf.template",
        "init.sql": "init.sql",
    }
    for destination, template in files.items():
        (profile / destination).write_text(
            render(source / template, replacements), encoding="utf-8"
        )

    postgres_database = "playground"
    (profile / "pg_service.conf").write_text(
        "[playground]\n"
        f"host=postgres\nport=5432\ndbname={postgres_database}\nsslmode=require\n"
        f"oauth_issuer={issuer}\noauth_client_id={client_id}\noauth_scope={scope}\n",
        encoding="utf-8",
    )
    metadata = {
        "name": args.name,
        "provider": "auth0",
        "mode": mode,
        "mappingMode": mode,
        "issuer": issuer,
        "discoveryUri": discovery_uri,
        "clientId": client_id,
        "audience": audience,
        "scope": scope,
        "identityClaim": identity_claim,
        "rolesClaim": roles_claim,
        "roles": roles,
        "hostPort": port,
        "postgresDatabase": postgres_database,
        "postgresRole": postgres_role,
        "testClients": ["node", "psql"] if client_mode == "both" else [client_mode],
    }
    (profile / "profile.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )
    print(f"created Auth0 playground profile: {profile}")
    print("review postgresql.conf, pg_hba.conf, pg_ident.conf, and init.sql before starting it")


def profiles():
    if not PROFILES.exists():
        return []
    return sorted(path for path in PROFILES.iterdir() if (path / "profile.json").is_file())


def list_profiles(_args):
    for profile in profiles():
        metadata = json.loads((profile / "profile.json").read_text(encoding="utf-8"))
        clients = ",".join(metadata["testClients"])
        print(f"{profile.name}\t{metadata['mode']}\t{clients}\t{metadata['issuer']}")


def get_profile(name):
    if not NAME_RE.fullmatch(name):
        raise ValueError("invalid profile name")
    profile = (PROFILES / name).resolve()
    if not (profile / "profile.json").is_file():
        raise ValueError(f"profile does not exist: {name}")
    return profile


def show(args):
    profile = get_profile(args.name)
    print((profile / "profile.json").read_text(encoding="utf-8"), end="")
    print("files:")
    for path in sorted(profile.iterdir()):
        print(f"  {path.name}")


def compose_command(profile, pg_major, port=None):
    command = ["docker", "compose", "-p", f"pg-oauth-{profile.name}-pg{pg_major}", "-f", str(ROOT / "compose.external.yml")]
    if pg_major == "19":
        command.extend(["-f", str(ROOT / "compose.external.pg19.yml")])
    environment = os.environ.copy()
    environment["PLAYGROUND_GENERATED_DIR"] = str(profile)
    metadata = json.loads((profile / "profile.json").read_text(encoding="utf-8"))
    environment["PLAYGROUND_POSTGRES_PORT"] = str(port or metadata.get("hostPort", 55432))
    environment.setdefault("PLAYGROUND_ROLE", metadata["postgresRole"])
    environment["PLAYGROUND_DATABASE"] = metadata["postgresDatabase"]
    return command, environment


def start(args):
    profile = get_profile(args.name)
    metadata = json.loads((profile / "profile.json").read_text(encoding="utf-8"))
    client_mode = args.client
    if client_mode is None:
        clients = metadata.get("testClients")
        if clients == ["node"]:
            client_mode = "node"
        elif clients == ["psql"]:
            client_mode = "psql"
        elif (isinstance(clients, list) and len(clients) == 2
              and set(clients) == {"node", "psql"}):
            client_mode = "both"
        else:
            raise ValueError("profile testClients must select node, psql, or both")
    command, environment = compose_command(profile, args.pg, args.port)
    try:
        subprocess.run(
            [*command, "up", "--build", "--wait", "postgres"],
            env=environment,
            check=True,
        )
        if client_mode in ("psql", "both"):
            subprocess.run(
                [*command, "run", "--rm", "psql"],
                env=environment,
                check=client_mode != "both",
            )
        if client_mode in ("node", "both"):
            subprocess.run(
                [*command, "run", "--rm", "oauth-node"],
                env=environment,
                check=True,
            )
    finally:
        if not args.keep_running:
            # Retain the named database volume for another run, but always
            # release containers, the network, and the selected host port.
            subprocess.run([*command, "down"], env=environment, check=False)


def reset(args):
    profile = get_profile(args.name)
    command, environment = compose_command(profile, args.pg)
    subprocess.run([*command, "down", "--volumes"], env=environment, check=True)


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    create_parser = commands.add_parser("create", help="interactively create a named profile")
    create_parser.add_argument("name")
    create_parser.add_argument("--mode", choices=MODES)
    create_parser.add_argument("--issuer")
    create_parser.add_argument("--discovery-uri")
    create_parser.add_argument("--client-id")
    create_parser.add_argument("--audience")
    create_parser.add_argument("--scope")
    create_parser.add_argument("--roles")
    create_parser.add_argument("--role")
    create_parser.add_argument("--client", choices=CLIENT_MODES)
    create_parser.add_argument("--port", type=int)
    create_parser.add_argument("--subject")
    create_parser.add_argument("--identity-claim")
    create_parser.add_argument("--roles-claim")
    create_parser.add_argument("--force", action="store_true")
    create_parser.set_defaults(function=create)

    list_parser = commands.add_parser("list", help="list generated profiles")
    list_parser.set_defaults(function=list_profiles)
    show_parser = commands.add_parser("show", help="show one generated profile")
    show_parser.add_argument("name")
    show_parser.set_defaults(function=show)
    for name, function in (("start", start), ("reset", reset)):
        command_parser = commands.add_parser(name)
        command_parser.add_argument("name")
        command_parser.add_argument("--pg", choices=("18", "19"), default="18")
        if name == "start":
            command_parser.add_argument(
                "--client", choices=("node", "psql", "both", "none")
            )
            command_parser.add_argument("--port", type=int)
            command_parser.add_argument(
                "--keep-running",
                action="store_true",
                help="leave PostgreSQL running for manual inspection",
            )
        command_parser.set_defaults(function=function)
    return result


def main():
    args = parser().parse_args()
    try:
        args.function(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"auth0-playground: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
