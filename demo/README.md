# Local OAuth demo

This Compose setup replaces the former `playground/` scripts and images. Use
`compose.yml` for the bundled Keycloak example or `compose.external.yml` with
your own reviewed issuer configuration. The former Auth0 helper and Node.js
client are no longer part of the demo; use a PostgreSQL 18 client with libpq
OAuth support for external providers.

Run this disposable stack from the repository root:

```sh
docker compose up --build -d
docker compose run --rm psql
```

The second command starts stock PostgreSQL 18 `psql` with libpq's device
authorization flow. Append `?user_code=` and the printed code to the URL it
prints, then open that complete URL in a browser. For example, if `psql` prints
`ABCD-EFGH`, open
`https://localhost:8443/realms/pg-oauth/device?user_code=ABCD-EFGH`.
This uses Keycloak's complete verification URL; the separate code-entry form
in the pinned Keycloak version did not complete correctly in local testing.
Sign in as **alice** with password **demo-alice-password** and approve the
**Connect to the demo PostgreSQL database** scope. Once authorized, `psql`
connects as the limited `app_reader` role. For a one-off query:

```sh
docker compose run --rm psql -c 'select current_user, system_user;'
```

Keycloak's demo admin is `demo-admin` / `demo-admin-password` at
<https://localhost:8443>. Your browser will warn about the locally generated
certificate. Export the demo CA with
`docker compose cp postgres:/certs/ca.crt ./demo-ca.crt` if you want to trust
it explicitly in your browser; do not use it outside this demo. The PostgreSQL
port is bound only to
`127.0.0.1:55432` by default; set `DEMO_POSTGRES_PORT` to choose another host
port. All credentials and certificates here are disposable test material.

The Compose services share Keycloak's network namespace so the issuer is
`https://localhost:8443/realms/pg-oauth` for the browser, validator, and
`psql`. PostgreSQL still requires a TLS connection and verifies the access
token against that exact issuer, the `https://postgres.example/` audience, the
`connect:postgres` scope, and the Keycloak signing key. The fixed demo subject
maps to `app_reader` through `pg_ident.conf`; other subjects and roles are
rejected. The client is public because libpq's built-in device flow uses a
public client; the requested scope is shown for user consent.

## Configure it

- [`postgresql.conf`](postgresql.conf) sets the audience, CA, and validator
  settings.
- [`pg_hba.conf`](pg_hba.conf) sets the exact issuer, scope, database, role,
  and usermap.
- [`pg_ident.conf`](pg_ident.conf) maps Keycloak subjects to database roles.
- [`keycloak-realm.json`](keycloak-realm.json) configures the demo client,
  audience, scope, and user.
- [`init.sql`](init.sql) creates roles on the first database initialization.
- [`psql.sh`](psql.sh) supplies matching libpq client settings.

The PostgreSQL configuration files are bind-mounted, so restart the PostgreSQL
service after editing them. Keycloak imports the realm when its container is
created. The `init.sql` file applies only to a new database volume. To discard
the demo database and generated certificates, run
`docker compose down --volumes`; this permanently deletes demo data. The
certificates expire after seven days, so reset the demo volumes before using
the stack again after that.

## Use an external provider

The separate [`compose.external.yml`](../compose.external.yml) starts
PostgreSQL without Keycloak. Copy the demo configuration into an ignored local
directory, then edit its issuer, audience, scope, usermap, and role setup for
your provider:

```sh
mkdir -p demo/local
cp demo/postgresql.conf demo/pg_hba.conf demo/pg_ident.conf demo/init.sql demo/local/
OAUTH_CONFIG_DIR=./demo/local docker compose -f compose.external.yml up --build -d
```

Set `pg_oauth_validator.ca_file` to an appropriate trusted CA file for your
provider, or to `''` when its certificate chains to the container's system
trust store. A custom CA can be placed in `demo/local/` and referenced as
`/demo/local/<filename>` in `postgresql.conf`. Use a PostgreSQL 18 client with
libpq OAuth support and a connection string containing the same exact
`oauth_issuer`, a client ID, and TLS verification of the demo PostgreSQL
certificate. Export its CA with
`OAUTH_CONFIG_DIR=./demo/local docker compose -f compose.external.yml cp postgres:/certs/ca.crt ./demo-ca.crt`.
The external provider and client credentials remain yours to configure; the
server denies access if those settings or token claims do not match.

This stack is for local evaluation. It includes known demo credentials and
should not be exposed on a shared network or reused as production
configuration. In particular, the local peer recovery rule, fixed demo
subject, and public device client are deliberate demo choices.
