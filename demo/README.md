# Try PostgreSQL OAuth with Keycloak

In this quickstart, you will connect to PostgreSQL as `app_reader` using an
access token from a local Keycloak realm. The stack uses Docker Compose and
includes PostgreSQL 18, the validator, Keycloak, and a matching `psql` client.
You need Docker with Compose and a browser; you do not need a local PostgreSQL
installation or a provider account.

1. From the repository root, build and start the stack:

   ```sh
   docker compose up --build -d
   ```

   The first build may take a few minutes. Check `docker compose ps` for a
   healthy `postgres` service if startup is still in progress.

2. Start a connection and request a query:

   ```sh
   docker compose run --rm psql -c 'SELECT current_user, system_user;'
   ```

   Keep this terminal open. `psql` prints a device code and waits while you
   authorize the connection in your browser.

3. Open `https://localhost:8443/realms/pg-oauth/device?user_code=CODE`, replacing
   `CODE` with the value printed by `psql`. For example, code `ABCD-EFGH`
   gives `https://localhost:8443/realms/pg-oauth/device?user_code=ABCD-EFGH`.
   Use the complete URL with `user_code`; the separate Keycloak code-entry form
   did not complete correctly in local testing with the pinned version.

4. Sign in as **alice** with password **demo-alice-password**. Approve the
   **Connect to the demo PostgreSQL database** scope on the consent page. Return
   to the terminal; the query should complete with `current_user` set to
   `app_reader` and `system_user` set to
   `oauth:00000000-0000-0000-0000-000000000001`.

Run `docker compose run --rm psql` for an interactive SQL session. Each new
connection starts another device authorization. `psql` obtains an access
token and presents it to PostgreSQL. The validator checks its signature,
issuer, audience, expiry, and required scope before PostgreSQL applies the
usermap.

Your browser may warn about the locally generated HTTPS certificate. To trust
this test CA explicitly, export it with
`docker compose cp postgres:/certs/ca.crt ./demo-ca.crt` and import that file
into your browser or operating-system trust store. Do not use this CA outside
the local stack. Keycloak's demo admin is `demo-admin` /
`demo-admin-password` at <https://localhost:8443>.

Stop the stack with `docker compose down`. To also delete its database and
certificates, run `docker compose down --volumes`; that permanently deletes
the disposable data. The certificates expire after seven days, so reset the
volumes before using the stack again after that. PostgreSQL is published only
on `127.0.0.1:55432` by default; set `DEMO_POSTGRES_PORT` before startup to
choose another host port.

## How the example is wired

The Compose services share Keycloak's network namespace so the issuer is
`https://localhost:8443/realms/pg-oauth` for the browser, validator, and
`psql`. PostgreSQL requires TLS and checks the exact issuer, the
`https://postgres.example/` audience, the `connect:postgres` scope, and the
Keycloak signing key. The fixed sample subject maps to `app_reader` through
`pg_ident.conf`; other subjects and roles are rejected. The client is public
because libpq's device flow uses a public client, and the connection scope is
shown for user consent.

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
created. The `init.sql` file applies only to a new database volume.

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

This Compose setup replaces the former `playground/` scripts and images. Use
`compose.yml` for the bundled Keycloak example or `compose.external.yml` with
your own reviewed issuer configuration. The former Auth0 helper and Node.js
client are no longer included; use a PostgreSQL 18 client with libpq OAuth
support for an external provider.
