# Development and verification

Engineering and review rules are in [AGENTS.md](../AGENTS.md). Use PostgreSQL
18 or 19; other majors are rejected. RPM-specific commands are in
[rpm-platforms.md](rpm-platforms.md).

## Container workflow

Docker with Compose provides the pinned toolchains without local PostgreSQL
dependencies. Source is mounted read-only and separate named volumes retain
incremental output for each major:

```sh
docker compose -f compose.test.yml build
docker compose -f compose.test.yml run --rm pg18
docker compose -f compose.test.yml run --rm pg19
```

The default developer gate runs strict builds and unit/component tests, omits
release-package reproducibility, and runs integration tests with two workers
excluding `slow` tests. Run the complete integration suites with:

```sh
docker compose -f compose.test.yml run --rm pg18 dev-test-full-pg18
docker compose -f compose.test.yml run --rm pg19 dev-test-full-pg19
```

For a shorter loop:

```sh
docker compose -f compose.test.yml run --rm \
  -e DEV_CHECK_TARGETS='check-policy check-claims check-identity' \
  pg18 dev-check-pg18

docker compose -f compose.test.yml run --rm \
  -e INTEGRATION_TEST_ARGS='-k direct_identity_and_delegated_roles' \
  pg18 dev-integration-pg18
```

Rebuild images after changing installers, the development Containerfile, package
versions or image digests. `docker compose -f compose.test.yml down --volumes`
removes compiler caches.

## Native builds

Require C17, GNU Make/PGXS, PostgreSQL server headers, `nm`, pkg-config, system
OpenSSL and libcurl, and the reviewed Jansson/libjwt sources. Integration also
requires Python 3, pytest, `runuser`, server programs and matching libpq headers.
The installers pin commits and archive hashes and install PIC static archives
into `/usr/local`; distribution Jansson archives may lack PIC.

```sh
./scripts/ci/install-jansson.sh
./scripts/ci/install-libjwt.sh
make test-pg18 PG18_CONFIG=/path/to/postgresql-18/bin/pg_config
make test-pg19 PG19_CONFIG=/path/to/postgresql-19/bin/pg_config
```

These targets isolate all build products under `build/pg18` and `build/pg19`
and include `verify` and the complete `integrationcheck`. `make verify-all`
runs both with the corresponding configurations. Direct PGXS builds are also
supported:

```sh
make clean
make verify PG_CONFIG=/path/to/postgresql/bin/pg_config
make integrationcheck PG_CONFIG=/path/to/postgresql/bin/pg_config
```

`verify` checks strict compilation, exported initialization and fail-closed
callbacks, unit/component behavior, dependency installers, demo configuration,
release artifacts, and runtime libcurl/DNS requirements. `integrationcheck`
starts isolated real servers and a local HTTPS issuer; it checks authorization,
TLS, hostile transport, reloads, rotation, outage/recovery, concurrency and
redaction. PostgreSQL 19 additionally tests custom HBA policy options.

## Quality and packaging gates

```sh
sh scripts/ci/check-source-tree.sh
./scripts/ci/install-pgindent.sh
make formatcheck
make sanitizercheck
make fuzz-smoke
make packagecheck
```

Pass `PG_CONFIG` when needed. Formatting uses pinned PostgreSQL 19 beta 3
`pgindent`, `pg_bsd_indent` 2.1.3 and the checked-in typedef list. Its installer
requires a compiler, Bison and Flex and writes under `.tools/`. `make format`
applies formatting. Sanitizers require Clang ASan/UBSan runtimes; fuzz smoke
runs bounded libFuzzer targets. Individual `fuzz-*` targets and
`make dependency-spike SANITIZE=1` are available in the Makefile.

`packagecheck` verifies staged contents, permissions, callback symbol, dynamic
dependencies and optional LLVM bitcode. `make installedcheck` installs into the
real PostgreSQL directories and tests that library; run it only with the required
privileges in a disposable container. [Release preparation](release-readiness.md)
adds reproducibility and lifecycle gates.

CI covers GCC/Clang builds on both majors, installed paths, formatting, static
analysis, sanitizers and fuzz smoke. Inspect [.github/workflows](../.github/workflows/)
for exact invocations. Source-tree hygiene runs on the host: pinned containers
lack Git and checkout falls back to a tarball, preventing an untracked-file check
inside them. Debian image digests, snapshot and exact package versions in
`scripts/ci/install-debian-dependencies.sh` form one reviewed input set; update
them together without unversioned fallback installs.

## Interoperability and test limits

```sh
make interop-keycloak
```

This opt-in Compose suite uses pinned Keycloak 26.5.0 and real PostgreSQL 19
OAuth connections. It checks access-token acceptance, ID-token and authorization
rejection, rotation, outage and recovery. See
[provider compatibility](../PROVIDER-COMPATIBILITY.md) for evidence limits.
Deterministic local-issuer/cache tests remain the primary regression coverage;
live-provider tests do not replace them.

The local DNS shim checks lookup failure and cancellation during a delayed
threaded lookup on Linux. Repeat cancellation testing with deployed libcurl/DNS.
CI robustness ceilings are five seconds for cache exhaustion and twelve seconds
for the mixed unknown-key/valid-token burst; these are regression limits, not
throughput benchmarks or production-readiness claims.
