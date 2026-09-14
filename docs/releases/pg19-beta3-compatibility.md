# PostgreSQL 19 Beta 3 compatibility evidence

- **Review date:** 2026-09-13
- **Base revision:** `2b76078a81b6a9e708c943e7cffd398d5723f2b5`
- **PostgreSQL package:** `19~beta3-1.pgdg12+1`
- **Container:** `postgres:19beta3-bookworm@sha256:c2f9db76caf60eda17be053e222349f4f3921b514b30a660c74dd9d9b976806f`
- **Architecture tested:** `aarch64`
- **Status:** local and immutable candidate-head compatibility gates complete;
  preview-only support retained

This record covers the PostgreSQL 19 Beta 3 compatibility refresh. PostgreSQL
19 remains preview-only. The PostgreSQL project explicitly advises against
running Beta 3 in production:

- <https://www.postgresql.org/about/news/postgresql-186-1711-1615-1519-1424-and-19-beta-3-released-3365/>

## Contract review

The implementation was checked against the PostgreSQL 19 documentation and
the exact installed Beta 3 headers used by the build.

- `_PG_oauth_validator_module_init` returns a server-lifetime `static const`
  callback table with `PG_OAUTH_VALIDATOR_MAGIC`, a validation callback, and a
  PostgreSQL 19 startup callback, as required by the initialization contract:
  <https://www.postgresql.org/docs/19/oauth-validator-init.html>.
- The validation callback matches the Beta 3 signature. It initializes
  `authorized`, `authn_id`, and `error_detail` to denial values before doing
  work; returns `false` for internal failures; returns `true` with
  `authorized=false` for token rejection; and returns a `palloc`-owned
  authenticated identity only after complete validation. This matches
  <https://www.postgresql.org/docs/19/oauth-validator-callbacks.html>.
- The PostgreSQL 19 startup callback registers `validator.policy` with
  `RegisterOAuthHBAOptions()`. Validation retrieves it with
  `GetOAuthHBAOption()` and treats unknown policy names as configuration
  failure. This matches
  <https://www.postgresql.org/docs/19/oauth-validator-hba.html>.
- The matched HBA issuer, required scope, and delegation flag remain isolated
  in `src/hba_policy.c`. The callback copies no PostgreSQL-owned HBA string
  beyond its lifetime.
- The PGXS build succeeded with strict C17 warnings, produced the expected
  module and LLVM bitcode, exported the initialization symbol, dynamically
  loaded the module, and exercised its fail-closed callback behavior.

No Beta 3 contract incompatibility was found. Final PostgreSQL 19 support still
requires the separate beta-to-final review and gates in
`docs/release-readiness.md`.

## Local gate results

All commands below used the pinned container above and completed successfully.

1. `docker compose -f compose.test.yml run --rm pg19 test-pg19`
   - strict build and unit/component verification passed;
   - real PostgreSQL integration suite: **22 passed**;
   - TLS, hostile transport, cache rotation/outage, concurrent cold-cache
     waiting, robustness, and diagnostic-redaction cases were included.
2. `make sanitizercheck fuzz-smoke installedcheck` in an executable isolated
   build volume
   - AddressSanitizer and UndefinedBehaviorSanitizer boundary suite passed;
   - six fuzz-smoke targets completed **2,000 runs each** without a finding;
   - staged/installed-path validation passed;
   - installed-module integration suite: **22 passed**.
3. Pinned `pgindent` formatting check and Clang static analysis
   - formatting passed;
   - `scan-build --status-bugs --keep-going ... make verify CC=clang` reported
     **No bugs found**.
4. Two `release-package` builds with the same version, source timestamp, base
   revision, compiler, dependencies, and PostgreSQL installation
   - archive digests and unpacked manifests matched;
   - the artifact checker confirmed PostgreSQL major **19**.
5. `make interop-keycloak`
   - Keycloak 26.5.0 RFC 9068 access-token acceptance and adversarial
     rejections: **1 passed**.
6. `docker compose -f compose.test.yml run --build --rm pg18 test-pg18`
   - cross-major regression check for the shell-invocation portability change:
     **20 passed, 2 PostgreSQL-19-only tests skipped**, using the refreshed
     PostgreSQL 18.6 image and package pins.

The first container attempt also exposed that `/source` is deliberately mounted
`noexec`: Makefile recipes executed repository shell scripts directly. The
committed fix invokes those scripts through `sh`. Candidate-head revision
`59be9f4c1e91eb164c41bd06892f8fc47625d091` subsequently passed CI and the
release-candidate workflow; its PostgreSQL 19 Beta 3 archive SHA-256 is
`b42e4e0d90b121bdacec5764aef9ece2340389aaf5a7bcd36cb8981e063eb393`.
This evidence does not approve PostgreSQL 19 final or authorize publication.
