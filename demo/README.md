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
PostgreSQL without Keycloak. Copy one profile into the ignored `demo/local/`
directory, then edit the provider values and choose one authorization mode:

```sh
mkdir -p demo/local
cp demo/provider.example.yml demo/local/provider.yml
# Edit demo/local/provider.yml
docker compose -f compose.external.yml up --build -d
```

The fields are:

| Field | Enter |
| --- | --- |
| `issuer` | Exact HTTPS `iss` value published by the provider. |
| `audience` | Exact PostgreSQL resource value in the access token's `aud`. |
| `scope` | One explicit database-access scope in the token's `scope` claim; `openid` alone is rejected. |
| `client_id` | Public OAuth device-flow client ID for bundled `psql`. |
| `identity_claim` | Stable claim for the authenticated ID, usually `sub`. |
| `identity_value` | Exact value of that claim for the one user allowed to connect as `app_reader` through `pg_ident.conf`. Use this or `role_claim`. |
| `role_claim` | Name of a signed token claim containing `app_reader` as a string or an array element. Delegates role authorization to the validator; use this or `identity_value`. |
| `ca_pem` | Optional inline PEM certificate for a private issuer CA. Omit for a publicly trusted CA. |

The profile contains no bearer token, private key, or client secret. The
renderer accepts a narrow flat YAML mapping, checks every field, and generates
PostgreSQL configuration, the HBA rule, role setup,
and matching libpq client settings at startup. A missing or invalid profile
stops startup. The fixed database is `demo` and the fixed role is
`app_reader`; the role is created only when the database volume is first
initialized. With `identity_value`, a `pg_ident.conf` map allows only that
exact identity. For a different user, change `identity_value` and recreate the
`postgres` container. The example value is a placeholder and cannot authorize
a real user. Never use a display name or mutable email address as a stable
identity unless your provider guarantees its suitability.

To allow tokens carrying the fixed role instead, delete the `identity_value`
line and add `role_claim: roles` (or your provider's actual claim name). The
signed claim must contain the exact string `app_reader`. The generated HBA rule
allows only that PostgreSQL role and enables `delegate_ident_mapping=1`; it
does not use a usermap. The token must still contain a valid stable identity
claim, usually `sub`, which PostgreSQL records as `system_user` for auditing.
Changing `identity_claim` chooses the recorded ID; it does not grant a role.
Issuers must configure the role claim for the access token used by this device
flow. Do not enable this mode with a claim that clients can set themselves.
If you used the former `OAUTH_CONFIG_DIR` setup, consolidate its issuer,
audience, scope, client ID, identity claim, and either mapped identity or role
claim into this YAML;
`OAUTH_CONFIG_DIR` no longer controls this Compose file. To roll back, use the
earlier Compose file and restore its four configuration files.

After the server is healthy, connect with the bundled client:

```sh
docker compose -f compose.external.yml run --rm psql -c 'SELECT current_user, system_user;'
```

The client prints a device authorization prompt. Complete it using the URL
and code shown by your provider. The server's generated TLS CA is trusted by
the bundled client. For a host client, export it with
`docker compose -f compose.external.yml cp postgres:/certs/ca.crt ./demo-ca.crt`
and use a PostgreSQL 18 libpq client with `sslmode=verify-full` plus the same
issuer, client ID, and scope. If your issuer uses a private CA, put its PEM
certificate in the optional `ca_pem: |` YAML block. The container trusts that
CA for both token validation and the bundled device flow.

To keep profiles for several providers, create another ignored YAML file and
select it with `OAUTH_PROFILE`:

```sh
OAUTH_PROFILE=./demo/local/other-provider.yml docker compose -f compose.external.yml up --build -d
OAUTH_PROFILE=./demo/local/other-provider.yml docker compose -f compose.external.yml run --rm psql
```

Stop one provider stack before starting another; both commands use the same
Compose project and persistent database volume. Recreate `postgres` and use
the matching `OAUTH_PROFILE` for `psql` after any profile edit. The validator
still requires a signed `at+jwt` access token, exact issuer and audience,
supported signing algorithm, valid expiry, and the configured `scope` claim.
The bundled client additionally needs a public device authorization flow.
Providers with opaque tokens, nonstandard scope claims, or other incompatible
behavior need separately reviewed validator support; this YAML does not add
provider-specific validation exceptions.

If the browser flow completes but `psql` reports an OAuth authentication
failure, check that the issued *access token* has the configured `aud`, scope,
and (for delegated mode) exact `app_reader` role claim. In mapping mode, check
that `identity_value` equals the token's selected identity claim. The token
must also satisfy the validator's JWT type and signing requirements. Avoid
putting complete tokens or their claims in shared logs or issue reports.

For WorkOS AuthKit Connect, its [access-token claim documentation](https://workos.com/docs/authkit/connect/token-claims)
says a device-flow user token's `aud` is the requested resource indicator, or
the environment client ID when no resource is requested; the requested
`client_id` may be different. The custom scope must be assigned to the OAuth
application and appear in the granted `scope`. WorkOS does not include roles
in Connect tokens by default. To use `role_claim: roles`, configure the
environment's [JWT template](https://workos.com/docs/authkit/jwt-templates)
to add `"roles": {{ organization_membership.roles }}`, and ensure an
organization membership grants the exact slug `app_reader`. The validator
still requires the token header and claims described above; completing the
browser consent step alone does not establish compatibility.

Run `docker compose -f compose.external.yml down` to stop. Add `--volumes` to
delete the disposable database and certificates. The local PostgreSQL
certificate expires after seven days; recreate its volumes when it does.

This stack is for local evaluation. It includes known demo credentials and
should not be exposed on a shared network or reused as production
configuration. In particular, the local peer recovery rule, fixed demo
subject, and public device client are deliberate demo choices.

This Compose setup replaces the former `playground/` scripts and images. Use
`compose.yml` for the bundled Keycloak example or `compose.external.yml` with
your own reviewed issuer profile.
