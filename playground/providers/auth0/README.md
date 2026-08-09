# Try the validator with Auth0

Auth0 access tokens can satisfy the validator's current strict token policy.
The playground provides a custom Node interoperability client for a complete
interactive device flow. It also retains stock `psql` to expose a known
libpq/Auth0 discovery incompatibility honestly.

## Current blocker: discovery URI

Auth0's exact issuer ends in `/`, for example
`https://example.eu.auth0.com/`, while its OpenID discovery document is at the
single-slash URL
`https://example.eu.auth0.com/.well-known/openid-configuration`.

PostgreSQL 18 and 19 currently form the URL advertised to libpq by appending
`/.well-known/openid-configuration` verbatim to the HBA issuer. With Auth0's
exact issuer this produces:

```text
https://example.eu.auth0.com//.well-known/openid-configuration
```

Auth0 returns 404 for that URL. Removing the trailing slash from the configured
issuer is unsafe and does not solve interoperability: Auth0's discovery
metadata and access tokens contain the trailing-slash issuer, and both libpq
and the validator intentionally require exact issuer matching.

Supplying the complete single-slash discovery URL to libpq avoids the 404, but
libpq then derives an issuer without the trailing slash and rejects Auth0's
exact issuer. PostgreSQL's source documents the explicit well-known-URI form in
[`generate_error_response()`](https://github.com/postgres/postgres/blob/REL_18_STABLE/src/backend/libpq/auth-oauth.c).
The PostgreSQL 19 source currently uses the same behavior.

The Node client avoids this client-side URL construction: it fetches the exact
configured discovery URL, verifies the metadata issuer byte-for-byte, performs
device authorization, and supplies the access token through node-postgres's
OAuth callback. The server HBA and validator still use Auth0's canonical issuer.
Do not remove Auth0's trailing issuer slash or weaken token validation.

## 1. Configure Auth0

In the Auth0 Dashboard:

1. Create an **API**. Give it an Identifier such as
   `https://postgres.example.internal/`, select RS256, and select the
   **RFC 9068** access-token profile (`rfc9068_profile`). Add the permission
   `connect:postgres`.
2. Create a **Native** application. Under Advanced Settings, enable the
   **Device Code** grant, use token endpoint authentication method **None**,
   and enable a connection containing your test user.
3. Authorize that application to request the API's `connect:postgres`
   permission.
4. In Tenant Settings > General > API Authorization Settings, set **Default
   Audience** to the API Identifier.

For direct or delegated role mapping, add a post-login Action whose values
come from administrator-controlled `app_metadata`, not user-editable
`user_metadata`. Replace the namespace with one controlled by your
organization:

```js
exports.onExecutePostLogin = async (event, api) => {
  const namespace = 'https://company.example'
  const roles = event.user.app_metadata?.postgres_roles
  if (Array.isArray(roles)) {
    api.accessToken.setCustomClaim(`${namespace}/postgres_roles`, roles)
  }
  if (typeof event.user.app_metadata?.postgres_role === 'string') {
    api.accessToken.setCustomClaim(
      `${namespace}/postgres_role`,
      event.user.app_metadata.postgres_role
    )
  }
}
```

Attach the Action to the Login flow. For the test user, set either
`postgres_role: "app_reader"` or
`postgres_roles: ["app_reader", "app_writer"]` in `app_metadata`. The HBA role
list remains an independent authorization boundary; a delegated token can use
only a role present in both places.

The last setting is necessary because PostgreSQL's built-in libpq device flow
can send scopes but has no Auth0-specific `audience` connection parameter.
Auth0 documents that Default Audience is equivalent to adding the audience to
every authorization request. Use a disposable tenant: this tenant-wide setting
can change tokens issued to other applications.

Do not request only `openid` or omit the custom API. That can produce a token
for `/userinfo`, not an access token for PostgreSQL.

Official references:

- [Auth0 Device Authorization Flow](https://auth0.com/docs/quickstart/native/device)
- [Auth0 access-token profiles](https://auth0.com/docs/secure/tokens/access-tokens/access-token-profiles)
- [Auth0 custom access-token claims](https://auth0.com/docs/secure/tokens/json-web-tokens/create-custom-claims)
- [Auth0 tenant Default Audience](https://auth0.com/docs/get-started/tenant-settings#api-authorization-settings)
- [PostgreSQL 18 OAuth connection parameters](https://www.postgresql.org/docs/18/libpq-connect.html)

## 2. Create a named playground profile

From `playground/`, run the interactive profile manager:

```sh
./auth0-playground.py create direct-test
./auth0-playground.py create mapped-test
./auth0-playground.py create roles-test
```

Numbered menus select the mapping mode, requested PostgreSQL role, and whether
the profile tests Node.js, psql, or both; enter the displayed number rather than
typing an option name. The CLI also asks for the exact Auth0 issuer, discovery
URL, Native application client ID, API audience, scope, PostgreSQL login roles,
and the claim relevant to the selected mode. No client secret is required. The
exact issuer must include its trailing `/`; the discovery URL uses the single
slash before `.well-known`.
The suggested host port advances for each existing profile so multiple modes
can run concurrently; it can also be overridden with `start --port PORT`.

Each command creates an independent ignored directory:

```text
.generated/auth0/direct-test/
  profile.json
  pg_service.conf
  postgresql.conf
  pg_hba.conf
  pg_hba.pg19.conf
  pg_ident.conf
  init.sql
```

These are complete, ordinary PostgreSQL files—not hidden mode switches. The
central `profile.json` records the provider settings, selected test clients,
and the PostgreSQL role, database, and host port used by both clients. Review
and edit the generated files before starting the test. Templates are under
`providers/auth0/examples/`.

List or inspect profiles with:

```sh
./auth0-playground.py list
./auth0-playground.py show roles-test
```

Start one selected profile. It runs the client or clients saved in
`profile.json`:

```sh
./auth0-playground.py start direct-test
./auth0-playground.py start mapped-test --pg 19
./auth0-playground.py start roles-test
```

Use `--client node`, `--client psql`, or `--client both` to override the saved
choice for one run. With Auth0, the
Node connection can succeed while stock `psql` is currently expected to fail
for the discovery reason above. Each profile and PostgreSQL major gets its own
Compose project and disposable database volume.

After the selected clients finish—or if authentication fails—the command stops
the profile's containers and releases its host port. It retains the database
volume for the next run. For manual inspection, leave PostgreSQL running with:

```sh
./auth0-playground.py start roles-test --keep-running
```

Use `reset` when the retained database volume should also be deleted.

Reset one profile's database after editing `init.sql` with:

```sh
./auth0-playground.py reset roles-test --pg 18
```

## 3. Connect with the Node interoperability client

Start PostgreSQL 18 and the interactive Node client:

```sh
./auth0-playground.py start direct-test
```

It prints Auth0's activation URL. Complete login as the test user configured
for the selected mapping mode; the client then connects and runs
`select current_user, current_database()`. The client uses the pure-JavaScript
node-postgres code from open
[PR #3694](https://github.com/brianc/node-postgres/pull/3694), pinned to commit
`68d58096e99296776a08d9c4333f5d742a8d2675` with a verified source-archive
checksum. It is test tooling, not a released dependency or production client.
Device authorization completes before the bounded PostgreSQL connection attempt
starts, so interactive login time does not consume the database connection
timeout.

The PostgreSQL connection uses the playground's disposable self-signed TLS
certificate without hostname verification. OAuth endpoints still require
normal HTTPS certificate validation.

## 4. Compare stock psql

The unmodified client remains available:

```sh
./auth0-playground.py start direct-test --client psql
```

With Auth0 it currently demonstrates the libpq issue described above; it is
expected to fail during discovery rather than reach the validator.

### Token diagnostics

When PostgreSQL authentication or the test query fails, the Node client
automatically decodes and prints the complete unverified JWT header and claim
object. This includes `sub`, roles, custom claims, issuer, audience, and timing
claims, so no debug flag is necessary. It deliberately does not print the raw
bearer token or signature because those are reusable credentials. Review the
claim output before sharing it: it can identify the test user, tenant, and
application. Decoding is diagnostic only—the validator continues to perform
the actual cryptographic verification.

For PostgreSQL 19 beta 2, its generated HBA selects the currently implemented
`default` validator policy explicitly. The Node client is unchanged:

```sh
./auth0-playground.py start direct-test --pg 19
```

To test another mapped role:

```sh
PLAYGROUND_ROLE=app_writer ./auth0-playground.py start direct-test
```

Reset the disposable database with:

```sh
./auth0-playground.py reset direct-test
```

No Auth0 secret or access token should be written to this repository. Generated
profiles are ignored, but still treat the working directory as sensitive local
state because tenant and user identifiers may appear in them.
