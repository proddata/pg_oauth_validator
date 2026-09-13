# pg_oauth_validator

Use OAuth 2.0 access tokens to authenticate PostgreSQL connections instead of
database passwords. `pg_oauth_validator` verifies a token from a trusted
identity provider, identifies the caller, and lets PostgreSQL apply its normal
role-mapping rules.


## What it does

```text
OAuth access token  ->  validate token and identity  ->  PostgreSQL role mapping
```

The validator accepts signed JWT access tokens from an explicitly configured
issuer. It checks the token's signature, issuer, audience, expiry, type,
required scopes, and stable identity before allowing PostgreSQL to continue
authentication.

By default, PostgreSQL decides which database role that external identity may
use: either its name must match the requested role, or a normal
`pg_ident.conf` usermap makes the decision.

### How a token becomes a PostgreSQL role

Every mode first validates the token completely. What differs is the final
role-authorization decision.

Let's assume a user requests the `app_reader` role in the `appdb` database.

```sh
psql "service=postgres-oauth" -U app_reader -d appdb
```

#### Option 1: Direct identity

When the sub claim of the validated token is the same as the requested PostgreSQL role, the validator allows the connection.


```text
token: { "sub": "app_reader" }
                    v
requested role: app_reader
                    v
PostgreSQL allows the connection
```

#### Option 2: Identity mapped by PostgreSQL

When a usermap is used and an external identity should connect as a different local
role (e.g. `app_reader`) `pg_ident.conf` decides which PostgreSQL role that identity may use.

```text
token: { "sub": "248289761001" }
                 v
     validated external identity (248289761001)
                 v
pg_ident.conf: external identity (248289761001)  ->  app_reader
                                                        v
                                     requested role: app_reader
```

#### Option 3: Delegated role claims

When the identity provider is intentionally the source of PostgreSQL role membership, the validator can check a configured claim for an exact role match. The validated token must contain the exact role the client requests, and the HBA rule must independently list that
role.

```text
token: { "sub": "alice", "roles": ["reporting", "app_reader"] }
                                             |
                                             v
 requested role: app_reader  <- exact member of roles claim; order does not matter
                 |
                 v
HBA allows app_reader  ->  PostgreSQL allows the connection
```

Configure both the validator and the
HBA rule:

```conf
# postgresql.conf
pg_oauth_validator.authorization_mode = 'claim_roles'
pg_oauth_validator.roles_claim = 'roles'

# pg_hba.conf
hostssl appdb app_reader,reporting 10.0.0.0/8 oauth \
    issuer="https://idp.example/" \
    scope="connect:postgres" \
    validator=pg_oauth_validator \
    delegate_ident_mapping=1
```

For that connection to succeed, the access token must contain `"app_reader"`
in its configured `roles` claim, and the HBA rule above must allow
`app_reader`. Requesting `reporting` instead requires both the same HBA
permission and `"reporting"` in the token's roles array.


## Quick start

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

For a complete, production-ready mapping example, including a `pg_ident.conf`
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
- [Playground](playground/README.md): disposable Docker/Podman environment for
  trying a real provider
- [Provider compatibility](PROVIDER-COMPATIBILITY.md): supported provider
  behavior and requirements

### Reference and contributors

- [Feature specification](FEATURES.md): supported behavior and security
  contract
- [Architecture](docs/architecture.md): validation pipeline, trust boundaries,
  caching, and PostgreSQL integration
- [Development](docs/development.md): prerequisites, builds, tests, fuzzing,
  and interoperability work
- [Project plan](oauth-validator-plan.md): rationale, threat model, and
  roadmap
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
