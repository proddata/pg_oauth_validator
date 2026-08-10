# Temporary implementation task board

This is a working coordination list, not a product specification. The security
contract remains in [`FEATURES.md`](FEATURES.md), design decisions remain in
[`oauth-validator-plan.md`](oauth-validator-plan.md) and the ADRs, and contributor
rules remain in [`AGENTS.md`](AGENTS.md).

Remove this file, or replace it with issue tracking, once the initial security
review backlog has been completed. A checked box means the acceptance checks
were run and the result was recorded; it does not merely mean code was written.

## Coordination rules

- Claim one task by adding an owner before editing. One owner per task.
- Do not combine tasks from different workstreams in one change.
- Respect the dependencies and file boundaries below. Coordinate before editing
  a file owned by another active task.
- Every security rule needs accepting, boundary, and adversarial coverage.
- Run the narrow suite first and both PostgreSQL 18 and 19 suites before merge
  when callback, configuration, cache, or integration behavior changes.
- Do not start deferred Milestones 3–5 from this board.

Status values are `ready`, `decision-required`, `blocked`, and `done`.

## Parallel wave 1

These tasks can proceed concurrently because their primary file ownership does
not overlap.

### INT-01 — Deterministic IdP transport-failure matrix

- **Status:** done
- **Owner:** int_01
- **Primary files:** `tests/integration/`, `docs/development.md`
- **Avoid editing:** validator/cache production code
- **Goal:** Prove the callback fails closed and recovers across hostile or
  broken discovery and JWKS responses using the local HTTPS Python IdP.
- **Cases:** connection timeout, total timeout, HTTP error, redirect, malformed
  JSON, wrong content type, oversized response, truncated response, and
  recovery to a valid response.
- **Acceptance:** every failure rejects a real PostgreSQL OAuth connection;
  bounds keep the suite prompt; recovery succeeds; response bodies, tokens,
  and secrets are absent from client errors and PostgreSQL logs; tests pass on
  PostgreSQL 18 and 19.

### CFG-01 — Decide the public cache-control contract

- **Status:** done
- **Owner:** root
- **Primary files:** `oauth-validator-plan.md`, `FEATURES.md`, a new or amended
  cache ADR, and `docs/configuration.md`
- **Avoid editing:** production implementation until the decision is approved
- **Goal:** Reconcile the planned settings (`jwks_default_ttl`,
  `cache_max_ttl`, `jwks_stale_grace`, `unknown_kid_refresh_cooldown`, and
  `cache_max_entries`) with the currently fixed implementation defaults.
- **Questions:** Which settings are operator-facing? What are their safe ranges
  and reload contexts? Does changing capacity require restart? Is stale JWKS
  use enabled in the first release or deliberately fixed to zero?
- **Proposed resolution:** retain ADR 0002's operator-facing settings and
  fail-closed defaults. Use ranges of 1s–1h for metadata/JWKS fallback TTLs,
  1s–24h for the local maximum TTL, 0–1h for JWKS stale grace, 1s–5min for
  unknown-`kid` cooldown, and 8–256 entries. Capacity is restart-only; all
  other settings are reloadable and participate in canonical cache keys.
- **Acceptance:** the decision states defaults, ranges, reload/cache
  invalidation, outage behavior, migration impact, and required positive and
  negative tests. No setting silently broadens trust.

### CI-01 — Complete production CI quality gates

- **Status:** done
- **Owner:** ci_01
- **Primary files:** `.github/workflows/`, `Makefile`, `scripts/ci/`,
  `docs/development.md`
- **Avoid editing:** validator behavior
- **Goal:** Audit the existing PostgreSQL 18/19 jobs and add missing canonical
  gates without duplicating build logic.
- **Cases:** compiler warnings with GCC and Clang, formatting check, static
  analysis, sanitizer execution, deterministic fuzz smoke tests, staged and
  installed package checks, clean-tree enforcement, dependency caching, and
  useful failure artifacts without credentials.
- **Acceptance:** every documented command runs locally or in the pinned CI
  images; jobs use pinned actions/images and least permissions; expected
  runtime stays bounded; the development guide names the canonical commands.

### REV-01 — Initial security-review traceability matrix

- **Status:** done
- **Owner:** rev_01
- **Primary files:** new file under `docs/security/`
- **Avoid editing:** implementation and tests
- **Goal:** Map every threat and initial acceptance criterion to the enforcing
  code boundary, positive test, negative test, and remaining gap.
- **Acceptance:** no criterion is marked covered without an exact test; gaps
  become task IDs on this board; deferred behavior is clearly distinguished
  from missing Milestone 1 behavior.

## Parallel wave 2

Start these after the named dependency is complete. They may then run in
parallel subject to their file boundaries.

### CFG-02 — Implement the approved cache-control contract

- **Status:** done
- **Owner:** cfg_02
- **Primary files:** `src/config.*`, `src/policy.*`, cache-policy wiring,
  configuration/unit tests, `docs/configuration.md`, `docs/operations.md`
- **Goal:** Implement only the settings approved by CFG-01 with safe startup or
  reload validation and complete cache-key separation.
- **Acceptance:** defaults preserve current fail-closed behavior; every bound
  has boundary tests; reload cannot reuse data under changed policy; both
  PostgreSQL majors pass unit and integration suites.

### INT-02 — End-to-end reload and cache invalidation matrix

- **Status:** done
- **Owner:** int_02
- **Primary files:** `tests/integration/`, `docs/configuration.md`
- **Goal:** Extend the existing audience reload test to each trust-relevant
  reloadable setting and prove old cache entries cannot authorize under a new
  policy.
- **Cases:** audience, algorithm allowlist, token type, identity claim, JWKS
  host allowlist, CA path, transport mode, token bound, and approved cache
  controls. Include restoring the old value without reusing an incompatible
  entry.
- **Acceptance:** real connections demonstrate rejection/acceptance on both
  PostgreSQL majors and secrets remain redacted.

### TEST-01 — Extract a reusable local IdP fixture

- **Status:** done
- **Owner:** test_01
- **Primary files:** `tests/integration/`
- **Goal:** Move the growing HTTPS IdP and PostgreSQL orchestration helpers out
  of `test_oauth.py` without changing behavior, so later fault cases remain
  readable and reusable.
- **Acceptance:** no weaker assertions, no test-only network exposure, clean
  shutdown on failures, deterministic request counters/control state, and the
  unchanged suite passes on PostgreSQL 18 and 19.

### OPS-01 — Release and deployment hardening checklist

- **Status:** ready
- **Owner:** root
- **Primary files:** `docs/operations.md`, packaging scripts, release workflow
- **Goal:** Define reproducible source/package provenance, dependency and CVE
  review, versioning, upgrade/rollback rehearsal, supported PostgreSQL build
  matrix, and PostgreSQL 19 beta-to-final transition checks.
- **Acceptance:** a release candidate can be built, inspected, installed,
  smoke-tested, rolled back, and reproduced from documented commands.
- **Implemented:** deterministic PG18/PG19 archives, checksums, embedded and
  sidecar manifests, source/dependency/compiler metadata, byte-identical
  double-build checks, and a manual non-publishing release-candidate workflow.
- **Remaining release evidence:** establish a reviewed Git revision and version,
  complete and date the dependency/license/vulnerability review, choose the
  signing/approval identities, run the workflow from that revision, and
  rehearse install/upgrade/rollback with the retained candidate artifacts.
- **2026-08-08 review:** the proposed `0.1.0-rc.1` dependency review is recorded
  in `docs/releases/0.1.0-rc.1-dependency-review.md` and is blocked pending
  third-party notices, immutable OS dependency inputs, exact runtime inventory,
  and completion of advisory and GitHub security review.
- **2026-08-08 remediation:** release builds now use a dated Debian snapshot,
  enforce exact direct and PostgreSQL development package versions, record the
  security-relevant package inventory, and bundle libjwt/Jansson notices. Fresh
  PG18 and PG19 beta 2 package smoke builds passed. Advisory disposition and
  GitHub security/approval evidence remain open before candidate promotion.
- **2026-08-10 Jansson provenance:** Debian's `libjansson-dev` ships a non-PIC
  static archive, so linking it into the module failed on Linux/amd64. Jansson
  is now built from pinned upstream 2.15.1 (commit
  `dbb5fb3636e155fccfce4cd215de752779bd6971`, archive SHA-256 recorded in
  `scripts/ci/install-jansson.sh` and `THIRD-PARTY-NOTICES.md`) as a PIC static
  archive, the release archive bundles the upstream `LICENSE`, and
  `check-static-link-dependencies` now proves each archive links into a shared
  object. The `0.1.0-rc.1` review records remain scoped to their reviewed
  revision and must be re-dated against the eventual candidate.
- **2026-08-08 advisory review:** exact-version findings and usage-bound
  dispositions are recorded in
  `docs/releases/0.1.0-rc.1-vulnerability-disposition.md`. No reachable,
  unpatched authentication-path advisory was identified. GitHub security
  results, the full candidate test/rehearsal evidence, and named approvals
  remain open.

## Security-review follow-ups

These gaps were identified by `REV-01`. They are part of Milestone 1 hardening,
not new provider or authorization features.

### SEC-CLAIMS-01 — Complete mandatory and wrong-type claim tests

- **Status:** done
- **Owner:** sec_claims_01
- **Primary files:** `tests/claims_test.c`
- **Goal:** Add exact rejection tests for missing `iss`, `aud`, and configured
  identity, plus wrong-type `iss`, `nbf`, `iat`, and identity values.
- **Acceptance:** each mandatory claim has one accepting and focused missing,
  wrong-type, and relevant boundary tests without weakening existing cases.

### SEC-LOG-01 — Diagnostic redaction matrix

- **Status:** done
- **Owner:** sec_log_01
- **Primary files:** `tests/integration/`, diagnostic documentation
- **Goal:** Exercise normal, debug, and error paths with unique token,
  signature, response-body, and claim sentinels. Configuration settings are
  not a secret-storage boundary because PostgreSQL core may log GUC values.
- **Acceptance:** no protected sentinel appears in client output or PostgreSQL
  logs; stable non-sensitive error codes remain actionable.

### ROBUST-01 — Sustained hostile-input resource bounds

- **Status:** done
- **Owner:** robust_01
- **Primary files:** integration/stress tests and CI smoke configuration
- **Goal:** Exercise bounded CPU, memory, cache-capacity, and concurrent hostile
  input behavior without turning normal CI into an unbounded load test.
- **Acceptance:** reviewed ceilings and time budgets are explicit; cache
  exhaustion fails closed; valid cached authentication remains responsive; no
  request or allocation growth is attacker-unbounded.

### TLS-01 — PostgreSQL and provider TLS trust matrix

- **Status:** done
- **Owner:** tls_01
- **Primary files:** `tests/integration/`, `docs/operations.md`
- **Goal:** Prove `verify-full` success plus hostname mismatch and incorrect or
  untrusted CA rejection for PostgreSQL client and provider HTTPS boundaries.
- **Acceptance:** valid chains and names succeed; every invalid chain/name
  fails closed; TLS diagnostics reveal no credential material on PostgreSQL 18
  or 19.
- **Verified:** deterministic integration coverage exercises libpq
  `verify-full` success, PostgreSQL hostname mismatch and untrusted CA, and
  provider HTTPS hostname mismatch and untrusted CA on both supported majors.
  Expired-certificate generation is tracked separately by `TLS-EXPIRY-01`.

### TLS-EXPIRY-01 — Deterministic expired-certificate rejection

- **Status:** done
- **Owner:** tls_expiry_01
- **Primary files:** `tests/integration/test_tls.py`, certificate fixture tooling
- **Goal:** Generate already-expired PostgreSQL and provider certificates with
  explicit validity timestamps, without changing the host clock or racing a
  near-term expiry boundary.
- **Acceptance:** both TLS boundaries reject the expired certificate on
  PostgreSQL 18 and 19; failures are bounded and reveal no credential material;
  the fixture works with the pinned CI OpenSSL version.
- **Verified:** a test-local CA issues localhost server certificates with fixed
  historical `notBefore`/`notAfter` values. Focused integration runs reject
  expired PostgreSQL and provider HTTPS leaf certificates within the bounded
  connection budget and preserve token redaction on both supported majors.

## Completed foundation

- [x] Strict JWT validation pipeline and normal `pg_ident.conf` mapping.
- [x] PostgreSQL 18 and PostgreSQL 19 beta build/integration matrix.
- [x] Shared metadata/JWKS cache with canonical policy-separated keys.
- [x] Deterministic local IdP rotation, key retirement, outage, freshness, and
  recovery integration coverage.
- [x] Concurrent unknown-`kid` refresh suppression across PostgreSQL backends,
  including the real cooldown boundary.
- [x] Pinned Keycloak 26.5 interoperability, key rotation, provider outage,
  recovery, TLS, negative authorization cases, and redaction checks.
- [x] Staged/install-path package validation in disposable CI images.

## Explicitly out of scope for this board

- Named provider compatibility profiles beyond strict-default interoperability.
- Auth0 and Entra production support claims.
- Delegated role authorization or automatic PostgreSQL role management.
- Opaque-token introspection, revocation services, or online authorization.
- Any weakening of exact issuer/audience/type/scope or normal usermap policy.
