# pg_oauth_validator

Use OAuth 2.0 access tokens to authenticate PostgreSQL connections instead of
database passwords. `pg_oauth_validator` verifies a token from a trusted
identity provider, identifies the caller, and lets PostgreSQL apply its normal
role-mapping rules.


## What it does

```text
OAuth access token  ->  validate token and identity  ->  PostgreSQL role mapping
```

The validator accepts signed JWT access tokens with `iss` and `sub` from an
explicitly configured issuer. It checks the token's signature, issuer,
audience, expiry, type,
required scopes, and stable identity before allowing PostgreSQL to continue
authentication.

By default, PostgreSQL decides which database role that external identity may
use: either its name must match the requested role, or a normal
`pg_ident.conf` usermap makes the decision.

### How identity and role selection work

The client requests a PostgreSQL role with `-U`; the token does not select it.
For the examples below, the client requests `app_reader` in `appdb`:

```sh
psql "service=postgres-oauth" -U app_reader -d appdb
```

The validator verifies the entire access token first. Every accepted token
must have `iss` and `sub`. By default, `identity_claim = 'sub'`, so the
validated `sub` becomes PostgreSQL's authenticated identity (`authn_id`).
`role_claim` is empty by default. The two settings have different jobs:

| Setting | What it means |
| --- | --- |
| `identity_claim` | Which validated string claim identifies the caller. PostgreSQL uses this value for direct matching or `pg_ident.conf` mapping, and records it as the authenticated identity. |
| `role_claim` | Which claim lists roles the caller may request when `delegate_ident_mapping=1`. It may contain one role string or an array of distinct role strings. It does not change the authenticated identity. |

Choose the mapping appropriate for your issuer and database roles:

| Mapping | Token claims shown | Who checks `app_reader`? | Authenticated identity |
| --- | --- | --- | --- |
| Direct | `"sub": "app_reader"` | PostgreSQL requires identity = requested role. | `app_reader` |
| PostgreSQL usermap | `"sub": "principal-123"` | PostgreSQL applies `map=oauthmap` and `pg_ident.conf`. | `principal-123` |
| Delegated roles | `"sub": "principal-123", "roles": ["app_reader", "reporting"]` | Validator requires an exact `app_reader` member; HBA must also allow that role. | `principal-123` |

**Direct:** Leave `role_claim` empty and use no `map=` or
`delegate_ident_mapping`. With the default identity claim, the token's `sub`
must equal the requested role:

```conf
# pg_hba.conf
hostssl appdb app_reader 10.0.0.0/8 oauth \
    issuer="https://idp.example/" scope="connect:postgres" \
    validator=pg_oauth_validator
```

**PostgreSQL usermap:** Leave `role_claim` empty and add `map=oauthmap` to the
HBA rule. PostgreSQL maps the authenticated identity to the requested role:

```conf
# pg_hba.conf
hostssl appdb app_reader 10.0.0.0/8 oauth \
    issuer="https://idp.example/" scope="connect:postgres" \
    validator=pg_oauth_validator map=oauthmap

# pg_ident.conf: map-name  authenticated-identity  PostgreSQL-role
oauthmap principal-123 app_reader
```

**Delegated roles:** Configure both `role_claim` and HBA delegation. The
validator checks that the claim contains the exact requested role; PostgreSQL
skips its normal identity comparison and `pg_ident.conf` mapping. The HBA
`USER` field independently limits which roles may be requested:

```conf
# postgresql.conf
pg_oauth_validator.role_claim = 'roles'

# pg_hba.conf
hostssl appdb app_reader,reporting 10.0.0.0/8 oauth \
    issuer="https://idp.example/" \
    scope="connect:postgres" \
    validator=pg_oauth_validator \
    delegate_ident_mapping=1
```

For this delegated rule, either `"roles": "app_reader"` or
`"roles": ["app_reader", "reporting"]` permits an `app_reader` request.
Requesting `reporting` requires that exact role in the claim and HBA `USER`
field. A `role_claim` without delegation, or delegation without a
`role_claim`, denies authentication. Delegation cannot be combined with
`map=`. The requested PostgreSQL role must exist with `LOGIN`. Keep the HBA
role list narrow, especially for privileged roles.

To identify callers by another stable, issuer-controlled string claim, set
`pg_oauth_validator.identity_claim = 'employee_id'` in `postgresql.conf`.
The token must still contain `sub`. With that setting, direct matching
compares `employee_id` to the requested role, a usermap maps
`employee_id`, and delegated mode still checks `roles` while returning
`employee_id` as the authenticated identity. PostgreSQL records that identity
in its OAuth connection log and exposes `system_user` as `oauth:<identity>`;
the requested database role remains separate.

## Quickstart: connect with Keycloak

This runnable example starts PostgreSQL 18 with the validator and a local
Keycloak realm. You need Docker with Compose; no local PostgreSQL installation
or provider account is required. From the repository root:

1. Start the services. The first build may take a few minutes.

   ```sh
   docker compose up --build -d
   ```

2. Connect with the bundled PostgreSQL 18 `psql` client and run a query:

   ```sh
   docker compose run --rm psql -c 'SELECT current_user, system_user;'
   ```

3. `psql` prints a device code and waits for authorization. In your browser,
   open `https://localhost:8443/realms/pg-oauth/device?user_code=CODE`, replacing
   `CODE` with the printed code. Sign in as **alice** with password
   **demo-alice-password**, then approve **Connect to the demo PostgreSQL
   database**. The locally generated HTTPS certificate may require you to trust
   the demo CA; the [Keycloak quickstart guide](demo/README.md) shows how.

4. Return to the terminal. The query should show `app_reader` as
   `current_user` and `oauth:00000000-0000-0000-0000-000000000001` as
   `system_user`. Run `docker compose run --rm psql` for an interactive session;
   each new connection starts a new device authorization.

Stop the services with `docker compose down`. To also delete the disposable
database and generated certificates, run `docker compose down --volumes`. The
[Keycloak quickstart guide](demo/README.md) explains the sample configuration
and how to use [`compose.external.yml`](compose.external.yml) with a single
ignored YAML profile for your own issuer. This stack uses published demo
credentials and is intended for local evaluation.

## Configure your own PostgreSQL server

Install the module built for the *same PostgreSQL major version* as your
server. The [operations guide](docs/operations.md) covers packaging and
installation.

Configure the PostgreSQL resource identifier (audience) in `postgresql.conf`:

```conf
oauth_validator_libraries = 'pg_oauth_validator'
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
```

Then add an OAuth rule to `pg_hba.conf`. This PostgreSQL 19 example accepts
connections over TLS from the named issuer when the token includes the required
connection scope:

```conf
hostssl appdb app_reader 10.0.0.0/8 oauth \
    issuer="https://idp.example/" \
    scope="connect:postgres" \
    validator=pg_oauth_validator \
    validator.policy=default
```

With the default identity settings, configure the identity provider to issue a
stable `sub` claim equal to `app_reader`, then create that PostgreSQL role:

```sql
CREATE ROLE app_reader LOGIN;
```

Reload PostgreSQL after changing configuration. PostgreSQL 18 uses the same
configuration except that it must omit `validator.policy=default`.

For a complete mapping example, including a `pg_ident.conf`
usermap, see the [configuration guide](docs/configuration.md).

## Secure production checklist

- Use `hostssl` and have clients verify the PostgreSQL server certificate.
- Configure one exact trusted issuer and an explicit audience.
- Require only the connection scopes your application needs.
- Use a stable, provider-controlled identity claim; do not use mutable email
  addresses without an explicit policy decision.
- Restrict HBA rules to intended databases, roles, and networks.
- Keep a strongly protected, non-OAuth administrative recovery path.
- Test rejected tokens too: wrong audience, issuer, scope, token type, and
  expiry must all deny access.

The validator fails closed when configuration, token validation, key retrieval,
or required network dependencies fail. It does not log bearer tokens or
secrets in normal diagnostics.

## Supported today

- PostgreSQL 18, plus PostgreSQL 19 preview support
- Signed JWT OAuth access tokens
- Exact issuer and audience validation
- Discovery/JWKS retrieval with bounded shared caching
- Normal PostgreSQL identity matching and `pg_ident.conf` usermaps
- Optional, explicitly configured exact role claims

## Documentation

### Getting started and operating

- [Configuration](docs/configuration.md): settings, role mapping, reloads, and
  secure examples
- [Installation and operations](docs/operations.md): packages, upgrades,
  rollout, and rollback
- [Keycloak quickstart](demo/README.md): follow the Compose tutorial and adapt
  the sample for an external issuer
- [Provider compatibility](PROVIDER-COMPATIBILITY.md): supported provider
  behavior and requirements

### Reference and contributors

- [Feature specification](FEATURES.md): supported behavior and security
  contract
- [Architecture](docs/architecture.md): validation pipeline, trust boundaries,
  caching, and PostgreSQL integration
- [Development](docs/development.md): prerequisites, builds, tests, fuzzing,
  and interoperability work
- [Release readiness](docs/release-readiness.md): publication requirements and
  PostgreSQL 19 finalization gates
- [RPM platform work](docs/rpm-platforms.md): experimental Fedora/Enterprise
  Linux builds and required SELinux gates
- [Architecture decisions](docs/adr/): reviewed technical and dependency
  decisions

## Developing locally

Install the reviewed, pinned libjwt dependency, then build and test against
PostgreSQL 18 or 19:

```sh
./scripts/ci/install-libjwt.sh
make clean
make verify
make integrationcheck
```

For normal development, use separate build trees for each PostgreSQL major:

```sh
make test-pg18 PG18_CONFIG=/path/to/postgresql-18/bin/pg_config
make test-pg19 PG19_CONFIG=/path/to/postgresql-19/bin/pg_config
```

See the [development guide](docs/development.md) for prerequisites and the
full test suite.

## License

`pg_oauth_validator` is released under the [PostgreSQL License](LICENSE).
See [third-party notices](THIRD-PARTY-NOTICES.md) for incorporated and linked
components and their licenses.
