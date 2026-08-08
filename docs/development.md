# Development and verification

This guide covers local build and test workflows. See
[`../AGENTS.md`](../AGENTS.md) for the full engineering and review requirements.

## Requirements

- PostgreSQL 18 or 19 server development headers and PGXS;
- a C17 compiler;
- GNU Make or a compatible `make` implementation;
- `nm` for the exported-symbol check;
- Jansson, the system OpenSSL development package, libcurl, and libjwt 3.3.3
  or later.

The module build requires PIC static archives for Jansson and libjwt. The
checked-in installer produces the required libjwt archive.

The integration suite additionally requires Python 3, pytest, `initdb`,
`pg_ctl`, `runuser`, and a libpq development installation from the same
PostgreSQL major version.

PostgreSQL 18 is the stable production target. PostgreSQL 19 is supported as a
preview while it remains in beta. The build rejects headers from other major
versions.

## Build and verify

Until libjwt 3.3.3 is commonly packaged, reproduce the reviewed dependency:

```sh
./scripts/ci/install-libjwt.sh
```

CI and release-candidate containers install their Debian dependencies through
`scripts/ci/install-debian-dependencies.sh`. The script selects the dated
Debian snapshot recorded by the pinned base images and enforces reviewed exact
versions for every direct dependency and the PostgreSQL development package.
Update the snapshot, package versions, and container digests together; do not
replace the script with an unversioned `apt-get install` step.

If a supported `pg_config` is on `PATH`:

```sh
make clean
make verify
make integrationcheck
```

For normal development, prefer isolated version-specific build trees:

```sh
make test-pg18 PG18_CONFIG=/path/to/postgresql-18/bin/pg_config
make test-pg19 PG19_CONFIG=/path/to/postgresql-19/bin/pg_config
```

These targets keep compiled objects, LLVM bitcode, shared libraries, test
executables, and Python caches under `build/pg18` or `build/pg19`. `make
verify-all` runs both when both `PG18_CONFIG` and `PG19_CONFIG` are supplied or
otherwise resolve to the appropriate versions. Direct PGXS targets remain
available for conventional extension packaging.

To select an installation explicitly:

```sh
make clean
make verify PG_CONFIG=/path/to/postgresql/bin/pg_config
make integrationcheck PG_CONFIG=/path/to/postgresql/bin/pg_config
```

`make integrationcheck` starts isolated temporary PostgreSQL servers on
loopback, performs real libpq OAuth bearer exchanges against a local HTTPS test
issuer, and verifies successful usermap authorization, rejected administrative
role assumption, fail-closed behavior, shared-cache concurrency, and
bearer-token redaction. Its hostile-transport matrix covers bounded connection
and total timeouts, HTTP errors, redirects, malformed and truncated JSON,
incorrect content types, oversized responses, and recovery for both discovery
and JWKS. PostgreSQL 19 additionally exercises named validator-policy
selection.

PGXS provides the standard `make install` target. Treat current builds as
pre-release software until the initial security review and packaging work are
complete.

On the supported Linux packaging environment, validate a non-privileged staged
installation with:

```sh
make packagecheck
```

This checks the exact PGXS manifest, permissions, exported callback symbol,
installed documentation, LLVM bitcode when PostgreSQL enables it, and dynamic
dependencies. `make installedcheck` additionally installs into
`pg_config --pkglibdir` and runs the integration suite against that path; use
it only as root in a disposable container. Both targets run for PostgreSQL 18
and 19 in GitHub Actions.

## CI quality gates

The canonical source, sanitizer, and deterministic fuzz-smoke commands are:

```sh
make check-source-tree
make sanitizercheck
make fuzz-smoke
```

`sanitizercheck` requires Clang's AddressSanitizer and UndefinedBehaviorSanitizer
runtimes. GitHub Actions builds and tests PostgreSQL 18 and 19 with both GCC and
Clang, runs the sanitizer/fuzz gates once with PostgreSQL 19 and Clang, and
runs Clang static analysis in a separate pinned PostgreSQL 19 job. GCC jobs
also validate staged and real installed paths. Every job cleans and rejects
unexpected build products even after an earlier failure.

## Provider interoperability

The opt-in provider interoperability suite requires Docker with Compose:

```sh
make interop-keycloak
```

It starts digest-pinned Keycloak 26.5.0 over HTTPS, imports a minimal realm,
obtains genuine access and ID tokens, and runs them through PostgreSQL 19. The
suite is intentionally separate from deterministic offline CI. With
Keycloak's `access.token.header.type.rfc9068` client option enabled, it proves
that a genuine `typ=at+jwt` access token succeeds through normal
`pg_ident.conf` mapping. Same-realm ID tokens, wrong audience, missing scope,
wrong issuer, and unmapped-role attempts are rejected. The suite also rotates
Keycloak's active RSA signing key, verifies both the new key and the still-
published previous key, stops Keycloak to prove fail-closed outage behavior,
and restarts it to prove recovery. Keycloak's discovery and JWKS responses
disallow reusable caching, so deterministic local tests cover stale-cache and
unknown-key refresh-suppression policies separately.

The ordinary `integrationcheck` suite uses its local HTTPS Python IdP to cover
the provider-independent lifecycle deterministically on PostgreSQL 18 and 19.
It publishes overlapping old and new RSA keys during rotation, retires the old
key after cache refresh, proves that fresh cached material remains usable
during an outage, fails closed at the advertised freshness boundary, and
proves recovery after the IdP becomes available again. This local coverage is
the primary security regression test; the Keycloak suite only confirms real-
provider interoperability. The same suite sends concurrent tokens with
different unknown key IDs through independent PostgreSQL backends and asserts
that they are all rejected while causing only one JWKS refresh per real
30-second cooldown; valid tokens using a cached key remain available.

The CI robustness smoke uses the configured default cache capacity of 32 and
holds every slot in a distinct refresh before submitting 16 concurrent excess
requests. Excess work must fail closed without waiting for the simulated
network operations. A separate burst mixes 24 attacker-selected unknown key
IDs with eight valid cached-token connections; it permits one JWKS refresh,
requires every hostile connection to fail, and requires every valid connection
to remain responsive. The reviewed wall-clock ceilings are five seconds for
cache-exhaustion handling and twelve seconds for the mixed authentication
burst. These are regression ceilings, not throughput benchmarks; longer load
and memory profiling belong in scheduled robustness jobs rather than normal CI.

See [`../PROVIDER-COMPATIBILITY.md`](../PROVIDER-COMPATIBILITY.md) for the
broader provider matrix and current limitations.

## Dependency and fuzz checks

The pinned libjwt dependency and its reviewed boundary behavior can be checked
separately:

```sh
./scripts/ci/install-libjwt.sh
make dependency-spike SANITIZE=1
make fuzz-libjwt-spike
make fuzz-jwt-envelope
make fuzz-jwks
make fuzz-claims
make fuzz-identity
make fuzz-metadata
```

The installer pins both the upstream commit and archive SHA-256. The validation
core and callback use libjwt for signature verification. libjwt and Jansson are
linked as position-independent static code with local symbol binding to prevent
collision with PostgreSQL server symbols.
