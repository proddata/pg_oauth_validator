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

### REL-01 — Release approval and publication

- **Status:** in progress — automated candidate-head gates complete; human
  review, signing, tag, and publication intentionally deferred
- **Owner:** root
- **Scope:** Select the candidate revision/version; produce version-separated
  PostgreSQL 18 and PostgreSQL 19 preview artifacts; collect immutable build,
  dependency, advisory, GitHub security, reproducibility, installation,
  upgrade, rollback, SBOM, provenance, signing, independent-review, and
  release-owner evidence.
- **Exit:** Every required item in
  [`docs/release-readiness.md`](docs/release-readiness.md) is evidenced for the
  exact candidate commit and artifact digests. Publication remains blocked on
  any missing, skipped, flaky, or failed gate.

### PG19-01 — PostgreSQL 19 Beta 3 compatibility refresh

- **Status:** done for Beta 3 — local and immutable candidate-head gates complete
- **Owner:** root
- **Scope:** PostgreSQL 19 Beta 3 was released on 2026-08-13. Its immutable
  image, package, formatter, CI, release-candidate, playground, and
  interoperability pins are updated. Complete the PostgreSQL OAuth validator,
  custom-HBA, memory/error, and PGXS contract review and record all PG19
  quality, integration, TLS, robustness, package, and interoperability gates.
- **Exit:** Record the exact immutable Beta 3 inputs and successful evidence;
  retain the preview label. This is not PostgreSQL 19 final-release approval.
  Local results and the completed contract review are recorded in
  [`docs/releases/pg19-beta3-compatibility.md`](docs/releases/pg19-beta3-compatibility.md).

### PG18-02 — PostgreSQL 18 current-minor security refresh

- **Status:** done for candidate head — local and immutable gates complete;
  final human release approval remains under REL-01
- **Owner:** root
- **Scope:** PostgreSQL 18.6 superseded the currently pinned 18.4 package on
  2026-08-13 and includes security fixes. Refresh the immutable image and exact
  PGDG package pins together, review the 18.6 security and compatibility
  changes, and rerun all PostgreSQL 18 quality, integration, packaging,
  installation, upgrade, rollback, and interoperability gates.
- **Exit:** Record advisory dispositions and successful evidence for the exact
  18.6 inputs. Do not prepare the release candidate from the older 18.4 build.
  Local results and the security-impact review are recorded in
  [`docs/releases/pg18-18.6-security-refresh.md`](docs/releases/pg18-18.6-security-refresh.md).

### PKG-01 — Downstream distribution packaging contract

- **Status:** in progress — build-system and gate work complete; the tag-based
  source contract is documented but unexercised because no tag is published yet
- **Owner:** root
- **Scope:** Make this source tree packageable by a distribution without
  forking it. Per-library `JANSSON_LINK_MODE`/`LIBJWT_LINK_MODE` switches with
  static as the default and a named diagnostic for every unresolvable
  dependency; declared minimum versions for libjwt, Jansson, libcurl, and
  OpenSSL enforced before compilation; a `WERROR=0` opt-out so a released tag
  still builds on a newer toolchain; proof that `make all && make install`
  needs no network and no installer; and native aarch64 coverage alongside
  x86-64.
- **Exit:** Every published switch has a gate that fails when it regresses.
  Version floors, link modes, the offline guarantee, and the source-tarball and
  digest contract are recorded in
  [`docs/dependencies.md`](docs/dependencies.md),
  [`docs/rpm-platforms.md`](docs/rpm-platforms.md), and
  [`docs/release-readiness.md`](docs/release-readiness.md). The tarball contract
  remains unevidenced until REL-01 publishes a signed tag and the recorded
  checksums.

## Intentionally deferred

The following need a specification/ADR decision before work starts: named
provider profiles and per-rule audience policy; delegated role/group/
entitlement authorization; opaque-token introspection and revocation-aware
validation. They are not release blockers for the active strict JWT scope.
