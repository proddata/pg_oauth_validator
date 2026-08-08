# pg_oauth_validator

`pg_oauth_validator` is a production-oriented OAuth 2.0 access-token validator
for PostgreSQL 18 and 19.

The validator currently supports strict signed JWT access-token validation,
shared metadata/JWKS caching, stable external identity construction, and normal
PostgreSQL `pg_ident.conf` role mapping. It remains under active security review
and is not yet a tagged stable release.

PostgreSQL 18 is the stable production target. PostgreSQL 19 support is preview
only while it remains in beta and must not itself be used for production
deployments. Other PostgreSQL major versions are rejected by the build.

## Documentation

- [`docs/configuration.md`](docs/configuration.md) — secure configuration,
  identity mapping, reload behavior, and deployment constraints;
- [`docs/development.md`](docs/development.md) — prerequisites, builds, tests,
  fuzzing, and provider interoperability;
- [`docs/operations.md`](docs/operations.md) — installation, upgrades,
  rollback, and production rollout checks;
- [`docs/release-readiness.md`](docs/release-readiness.md) — reproducible
  release candidates, dependency review, provenance, and promotion gates;
- [`docs/architecture.md`](docs/architecture.md) — the implemented validation
  pipeline, trust boundaries, cache, and PostgreSQL integration;
- [`FEATURES.md`](FEATURES.md) — supported and planned product behavior;
- [`PROVIDER-COMPATIBILITY.md`](PROVIDER-COMPATIBILITY.md) — provider-specific
  compatibility status and requirements;
- [`oauth-validator-plan.md`](oauth-validator-plan.md) — design rationale,
  threat model, and roadmap;
- [`docs/adr/`](docs/adr/) — reviewed architectural and dependency decisions;
- [`AGENTS.md`](AGENTS.md) — engineering and contribution requirements.
- [`TASKS.md`](TASKS.md) — temporary dependency-aware implementation board.

## Quick start

Install the reviewed, pinned libjwt dependency before building:

```sh
./scripts/ci/install-libjwt.sh
```

With PostgreSQL 18 or 19's `pg_config` on `PATH`:

```sh
make clean
make verify
make integrationcheck
```

For normal development, prefer an isolated version-specific build tree:

```sh
make test-pg18 PG18_CONFIG=/path/to/postgresql-18/bin/pg_config
make test-pg19 PG19_CONFIG=/path/to/postgresql-19/bin/pg_config
```

See the [development guide](docs/development.md) for complete prerequisites,
other build layouts, and the additional test suites.

The opt-in Keycloak interoperability suite also exercises genuine signing-key
rotation and a complete provider stop/restart cycle; see the development guide
for its Docker requirements and scope.

## Minimum secure configuration

Audience has no safe universal default and must be configured explicitly:

```conf
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
```

Production OAuth HBA rules should use `hostssl`, specify a non-empty connection
scope, select the validator, and use normal PostgreSQL user mapping. For example,
on PostgreSQL 19:

```conf
hostssl all all 0.0.0.0/0 oauth \
    issuer=https://idp.example.com/ \
    scope="connect:postgres" \
    validator=pg_oauth_validator \
    validator.policy=default \
    map=oauth
```

The validator returns an issuer-qualified external identity:

```text
v1.<base64url(issuer)>.<base64url(configured-stable-claim)>
```

PostgreSQL then applies the selected `pg_ident.conf` map to decide whether that
identity may assume the requested database role. The validator does not enable
delegated identity mapping.

Read the complete [configuration contract](docs/configuration.md) before a
deployment. In particular, production use requires TLS, an exact trusted
issuer, an explicit audience, required scopes, and carefully reviewed JWKS host
and CA settings.

## Security status

The callback begins denied and returns success only after metadata, JWKS,
signature, claims, audience, token type, scopes, and stable identity all pass.
Client errors remain generic, and protected diagnostics are designed not to
include bearer tokens or secrets.

This is security-critical infrastructure under active review. Passing local
tests alone is not a production-readiness claim.
