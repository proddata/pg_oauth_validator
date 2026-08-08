# Remaining work after the Milestone 1 implementation

This is the short operational backlog for work that does not change the
Milestone 1 validation policy. The product contract remains in
[`../FEATURES.md`](../FEATURES.md), and the longer-term design remains in
[`../oauth-validator-plan.md`](../oauth-validator-plan.md).

## Implemented earlier than the original roadmap suggested

The original Milestone 2 list is substantially implemented: the module has a
shared bounded metadata/JWKS cache, strict TLS and HTTP transport controls,
stable sanitized diagnostics, configuration/cache separation, real PostgreSQL
integration tests, robustness tests, and local provider fixtures. These are
part of the current Milestone 1 release candidate rather than a reason to
broaden its authorization model.

The repository also has pinned PostgreSQL 18 and PostgreSQL 19 beta 2 build
environments, GCC and Clang jobs, static analysis, sanitizers, deterministic
fuzz smoke, formatting checks, staged and installed-path verification,
Keycloak interoperability, dependency/license records, and reproducible
release-package construction.

## Release-engineering work

- Exercise the same-environment reproducibility gate in GitHub Actions and
  retain its comparison evidence. Repeat it in independent fresh environments
  for release approval.
- Rehearse clean installation, upgrade, rollback, provider outage recovery,
  and protected administrator recovery for PostgreSQL 18.
- Define the approved artifact-signing mechanism and sign the archive,
  checksum, tag, and release record.
- Review GitHub dependency alerts, secret scanning, and code scanning for the
  candidate commit. Record dispositions rather than treating scanner output as
  an approval.
- Obtain independent security-sensitive review and release-owner approval.
- Publish a candidate only after every item in
  [`release-readiness.md`](release-readiness.md) has evidence for the exact
  commit and artifacts.

## PostgreSQL 19 finalization

PostgreSQL 19 remains preview-only. After its final release, replace beta pins,
review the final callback/HBA/PGXS contracts, rerun every quality and runtime
gate, and repeat coexistence, installation, upgrade, and rollback rehearsals.
A beta-built module must never be relabeled as a final PostgreSQL 19 artifact.

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
