#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
playground_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
profiles=$(mktemp -d)
trap 'rm -rf "$profiles"' EXIT HUP INT TERM

common_args="--issuer https://issuer.example/ --discovery-uri https://issuer.example/.well-known/openid-configuration --client-id public-client --audience https://postgres.example/ --scope connect:postgres --roles app_reader,app_writer --role app_writer --port 55439 --client both"

# common_args contains only constants owned by this test.
# shellcheck disable=SC2086
PLAYGROUND_PROFILES_DIR=$profiles python3 "$playground_dir/auth0-playground.py" create direct --mode direct $common_args --identity-claim https://company.example/postgres_role >/dev/null
# shellcheck disable=SC2086
PLAYGROUND_PROFILES_DIR=$profiles python3 "$playground_dir/auth0-playground.py" create mapped --mode issuer-qualified $common_args --subject 'auth0|alice' >/dev/null
# shellcheck disable=SC2086
PLAYGROUND_PROFILES_DIR=$profiles python3 "$playground_dir/auth0-playground.py" create roles --mode claim-roles $common_args --roles-claim https://company.example/postgres_roles >/dev/null

grep -F "identity_format = 'direct'" "$profiles/direct/postgresql.conf" >/dev/null
grep -F 'hostssl playground  app_reader,app_writer' "$profiles/direct/pg_hba.conf" >/dev/null
grep -F 'CREATE ROLE app_writer LOGIN;' "$profiles/direct/init.sql" >/dev/null
grep -F 'oauthmap  v1.aHR0cHM6Ly9pc3N1ZXIuZXhhbXBsZS8.YXV0aDB8YWxpY2U  app_reader' "$profiles/mapped/pg_ident.conf" >/dev/null
grep -F "authorization_mode = 'claim_roles'" "$profiles/roles/postgresql.conf" >/dev/null
grep -F 'delegate_ident_mapping=1' "$profiles/roles/pg_hba.pg19.conf" >/dev/null
grep -F '"mappingMode": "claim-roles"' "$profiles/roles/profile.json" >/dev/null
grep -F '"postgresRole": "app_writer"' "$profiles/roles/profile.json" >/dev/null
grep -F '"postgresDatabase": "playground"' "$profiles/roles/profile.json" >/dev/null
grep -F '"testClients": [' "$profiles/roles/profile.json" >/dev/null

# Finite interactive choices accept number keys: direct mode, app_writer, psql.
printf '1\n2\n2\n' | PLAYGROUND_PROFILES_DIR=$profiles python3 "$playground_dir/auth0-playground.py" create keyboard --issuer https://issuer.example/ --discovery-uri https://issuer.example/.well-known/openid-configuration --client-id public-client --audience https://postgres.example/ --scope connect:postgres --roles app_reader,app_writer --port 55440 --identity-claim https://company.example/postgres_role >/dev/null
grep -F '"mappingMode": "direct"' "$profiles/keyboard/profile.json" >/dev/null
grep -F '"postgresRole": "app_writer"' "$profiles/keyboard/profile.json" >/dev/null
grep -F '"psql"' "$profiles/keyboard/profile.json" >/dev/null

test "$(PLAYGROUND_PROFILES_DIR=$profiles python3 "$playground_dir/auth0-playground.py" list | wc -l | tr -d ' ')" = 4

if PLAYGROUND_PROFILES_DIR=$profiles python3 "$playground_dir/auth0-playground.py" create bad --mode direct $common_args --roles 'app_reader,all bad' --identity-claim role >/dev/null 2>&1; then
	echo "error: unsafe PostgreSQL role was accepted" >&2
	exit 1
fi

echo "Auth0 playground profile tests passed"
