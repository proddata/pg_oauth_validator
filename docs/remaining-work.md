# Remaining work after the strict JWT implementation

This is the short operational backlog for work that does not change the
active validation policy. The product contract remains in
[`../FEATURES.md`](../FEATURES.md), and the longer-term design remains in
[`../oauth-validator-plan.md`](../oauth-validator-plan.md).

## Implemented earlier than the original roadmap suggested

The original Milestone 2 list is substantially implemented: the module has a
shared bounded metadata/JWKS cache, strict TLS and HTTP transport controls,
stable sanitized diagnostics, configuration/cache separation, real PostgreSQL
integration tests, robustness tests, and local provider fixtures. These are
part of the current strict JWT release scope rather than a reason to
broaden its authorization model.

The repository also has pinned PostgreSQL 18 and PostgreSQL 19 beta 3 build
environments, GCC and Clang jobs, static analysis, sanitizers, deterministic
fuzz smoke, formatting checks, staged and installed-path verification,
Keycloak interoperability, dependency/license records, and reproducible
release-package construction.

## Release-engineering work

- Repeat reproducibility in independent fresh environments for release
  approval. The exact-candidate same-environment GitHub Actions gate passed.
- Retain the first approved artifact so the next release can rehearse upgrade,
  replacement, rollback digest comparison, and recovery against it. Clean
  installation, installed-path testing, provider outage recovery, and
  protected administrator recovery have passed for this candidate.
- Define the approved artifact-signing and provenance mechanism, generate an
  SBOM, and sign the archive, checksum, tag, and release record.
- Recheck GitHub dependency alerts, secret scanning, and code scanning at the
  exact revision selected for publication. The candidate-head checks were
  clean, but scanner output is evidence rather than approval.
- Obtain independent security-sensitive review and release-owner approval.
- Verify repository rules require the intended reviews and successful status
  checks before a production release can merge.
- Publish a candidate only after every item in
  [`release-readiness.md`](release-readiness.md) has evidence for the exact
  commit and artifacts.

## PostgreSQL 19 finalization

PostgreSQL 19 remains preview-only. After its final release, replace beta pins,
review the final callback/HBA/PGXS contracts, rerun every quality and runtime
gate, and repeat coexistence, installation, upgrade, and rollback rehearsals.
A beta-built module must never be relabeled as a final PostgreSQL 19 artifact.

PostgreSQL 19 Beta 3 (released 2026-08-13) is the current pinned preview
build. Its compatibility evidence is recorded with `PG19-01` in
[`../TASKS.md`](../TASKS.md); a final-release update still requires the full
finalization gates above.

## Product work requiring explicit decisions

Do not start these items as incidental hardening work:

- named provider profiles and per-rule audience policy;
- additional Auth0, Entra, Okta, or other provider interoperability contracts;
- delegated role/group/entitlement authorization;
- opaque-token introspection and revocation-aware online validation;
- changes to required-scope, audience, identity, cache, or outage policy.

Each item changes or extends the trust contract and therefore requires an ADR
or an approved update to `FEATURES.md`, including adversarial tests and
migration guidance.
