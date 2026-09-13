# Active delivery backlog

This is the compact active backlog for the initial release candidate. Product
behavior remains specified in [`FEATURES.md`](FEATURES.md), architecture and
security decisions in [`oauth-validator-plan.md`](oauth-validator-plan.md) and
the ADRs, and release evidence in
[`docs/release-readiness.md`](docs/release-readiness.md).

The detailed initial-security-review coordination board was retired on
2026-09-13. Its implementation work is complete; the evidence remains in the
test suite, CI configuration, ADRs, and
[`docs/security/initial-review-traceability.md`](docs/security/initial-review-traceability.md).
Do not reopen it by treating deferred product milestones as cleanup work.

## Active work

### CACHE-01 — Bounded concurrent cold-cache wait

- **Status:** done
- **Owner:** root
- **Scope:** Complete the existing `refresh_wait_timeout` implementation and
  record focused unit and PostgreSQL 18/19 integration evidence. It must retain
  fail-closed behavior, never hold a PostgreSQL lock while waiting, and support
  an explicit zero-timeout fail-fast mode.
- **Exit:** Review the complete diff and record the exact test commands and
  outcomes before merge.

### REL-01 — Release candidate evidence and approval

- **Status:** ready
- **Owner:** unassigned
- **Scope:** Select the candidate revision/version; produce version-separated
  PostgreSQL 18 and PostgreSQL 19 preview artifacts; collect immutable build,
  dependency, advisory, GitHub security, reproducibility, installation,
  upgrade, rollback, signing, independent-review, and release-owner evidence.
- **Exit:** Every required item in
  [`docs/release-readiness.md`](docs/release-readiness.md) is evidenced for the
  exact candidate commit and artifact digests. Publication remains blocked on
  any missing, skipped, flaky, or failed gate.

### PG19-01 — PostgreSQL 19 Beta 3 compatibility refresh

- **Status:** in progress
- **Owner:** root
- **Scope:** PostgreSQL 19 Beta 3 was released on 2026-08-13. Its immutable
  image, package, formatter, CI, release-candidate, playground, and
  interoperability pins are updated. Complete the PostgreSQL OAuth validator,
  custom-HBA, memory/error, and PGXS contract review and record all PG19
  quality, integration, TLS, robustness, package, and interoperability gates.
- **Exit:** Record the exact immutable Beta 3 inputs and successful evidence;
  retain the preview label. This is not PostgreSQL 19 final-release approval.

## Intentionally deferred

The following need a specification/ADR decision before work starts: named
provider profiles and per-rule audience policy; delegated role/group/
entitlement authorization; opaque-token introspection and revocation-aware
validation. They are not release blockers for the strict Milestone 1 scope.
