# Release readiness

This checklist defines the evidence required to publish a
`pg_oauth_validator` release. Passing it means a particular source revision and
its version-specific packages were reviewed and rehearsed; it does not by
itself claim that the project or every deployment is production-ready.

## Version and tag policy

- Use semantic versions of the form `MAJOR.MINOR.PATCH`. Versions in the
  `0.x.y` initial-development series are marked as GitHub prereleases even
  without a SemVer prerelease suffix. For later major versions, use a SemVer
  prerelease suffix such as `-rc.1` while the release is not approved for
  production. PostgreSQL 19 packages remain preview artifacts until the gates
  below are complete.
- The source version, release notes, package `BUILD-MANIFEST`, archive name,
  annotated Git tag, and published checksums must name the same version.
- Tag the exact reviewed commit as `vMAJOR.MINOR.PATCH` (including any
  prerelease suffix). Release tags must be annotated and signed by an approved
  release key. Do not move or reuse a published tag or version.
- Patch releases may fix defects without intentionally broadening trust.
  Configuration, identity, cache/outage, ABI, or trust-policy changes require
  explicit compatibility and migration notes and may require a minor or major
  version increase according to impact.

## Tag-triggered draft workflow

After the source revision has passed review, create and push a signed,
annotated `vMAJOR.MINOR.PATCH` tag (or a SemVer prerelease tag such as
`v0.1.0-rc.1`) pointing to a commit on `main`. The `Release draft` workflow
checks the tag name, annotated-tag type, target commit, main-branch ancestry,
and GitHub's signature-verification result. A signature GitHub cannot verify
blocks the workflow. The release owner must still confirm that the signer is
an approved release key; GitHub's verification alone does not establish that.

The workflow reuses the normal CI and CodeQL gates, builds the two PostgreSQL
release-candidate packages, runs the pinned Keycloak interoperability suite,
verifies the downloaded artifacts and their embedded manifests, and creates a
**draft** GitHub Release with `SHA256SUMS`. PostgreSQL 19 remains a preview
package. No tag-triggered job publishes the draft. The manually dispatched
`Release candidate` workflow remains available for rehearsals before tagging.

Before publishing the draft, complete the version-appropriate review,
fresh-environment rebuild, dependency review, install/upgrade/rollback
rehearsal, and approval steps below. Sign the final archives and checksum file
with the approved release key, upload and verify the signatures, and record the
release approver. Then
publish the draft without replacing the reviewed tag or artifacts. A rejected
candidate gets a new version and tag; do not move the existing tag.

## Limited prerelease retention decision, 2026-10-24

For the provisional **0.1.3 prerelease**, the deployment owner (`proddata`)
explicitly deferred additional durable storage while the project is not yet
production-ready. Existing CI retention is accepted for this prerelease:
RPM builder/candidate/evidence artifacts have a 90-day retention setting;
PostgreSQL release-candidate archives have a 14-day retention setting. Each
artifact's actual expiry is determined by its upload time and GitHub policy.
The locked builder capture predates the candidate, so a candidate rebuild does
not extend builder retention. Local copies are convenience copies, not a
verified backup.

This is a prerelease-only exception to the durable-retention gate below, not a
claim of durable recovery or a relaxation of integrity/authentication tests.
After expiry, archived inputs may be unavailable and exact reproduction or
rollback may no longer be possible. Missing locked inputs must still fail
closed; never substitute current repository packages. Before production
promotion, arrange durable retention and verify a restore, or capture/review new
inputs and rerun affected gates if the original inputs are unavailable.

The dependency/security and license review is owner-reported complete, subject
to the scoped Rocky exception in the RPM review record. Final candidate checks,
source/version identity, signatures and release-owner approval still apply.
No release tag or publication is authorized merely by preparing a candidate.

## Release record

Create an immutable release record containing:

- tag, full source commit, source-tree status, release version, and
  `SOURCE_DATE_EPOCH` (normally the tagged commit timestamp);
- build host/container image digests, compiler and linker versions, exact
  `pg_config --version`, architecture, and operating-system package versions;
- the dependency review described below;
- commands and results for every required check, including expected skips;
- archive names, archive SHA-256 files, and signatures;
- reviewer and release approver identities (the same maintainer may fill both
  roles for a `0.x` prerelease), known limitations, and rollback
  artifact identifiers.

The tree must contain no uncommitted or untracked release inputs. CI artifacts
are evidence only when they were built from the tagged commit with the same
pinned inputs.

The canonical Debian/PGDG dependency installation is
`scripts/ci/install-debian-dependencies.sh`. Its Debian snapshot timestamp,
exact direct-package versions, PostgreSQL development-package version, and
pinned container digest form one reviewed input set and must be updated
together. A missing version must fail installation; do not fall back to a
newer repository candidate. The generated manifest must contain the snapshot,
PGDG development version, and exact security-relevant runtime packages.

## PostgreSQL-specific artifacts

RPM candidates use a separate `RPM candidate rehearsal` workflow for all six
Fedora 43/44 and Rocky 10 architecture pairs. It loads reviewed complete builder
archives from the checked-in lock, builds on two fresh native runners, compares
complete retained bundles, and rehearses the exact rebuilt v0.1.2 source-release
baseline -> provisional 0.1.3 candidate -> retained baseline. See
[`rpm-platforms.md`](rpm-platforms.md#locked-release-candidate-inputs) for
versioned test contracts, sanitized evidence and retention requirements.
The baseline was not a previously published RPM. Dependency/vendor-advisory
review, durable retention and release signing remain explicit approval gates;
these tests alone do not establish production readiness. Existing Fedora 44
x86-64 enforcing evidence retains its documented scope. The tag-triggered draft
workflow below does not publish RPMs.

PostgreSQL does not provide cross-major server-module ABI compatibility. Build,
test, name, stage, publish, install, and roll back PostgreSQL 18 and 19 artifacts
separately. Never rename one major's package to serve the other.

Before packaging, run the isolated suites with exact versioned installations:

```sh
make test-pg18 PG18_CONFIG=/path/to/postgresql-18/bin/pg_config
make test-pg19 PG19_CONFIG=/path/to/postgresql-19/bin/pg_config
```

Build reproducible packages into separate output directories:

```sh
SOURCE_DATE_EPOCH=... make release-package \
  RELEASE_VERSION=... \
  PG_CONFIG=/path/to/postgresql-18/bin/pg_config \
  RELEASE_OUTPUT="$PWD/dist/pg18"

SOURCE_DATE_EPOCH=... make release-package \
  RELEASE_VERSION=... \
  PG_CONFIG=/path/to/postgresql-19/bin/pg_config \
  RELEASE_OUTPUT="$PWD/dist/pg19"
```

`release-package` builds in a temporary directory, runs the staged-install
manifest checker, records the exact PostgreSQL release and per-file SHA-256
digests in `BUILD-MANIFEST`, normalizes archive metadata, and emits an archive
SHA-256 file. Inspect both manifests and prove each package rejects installation
into the other major's test environment. Run `make installedcheck` for each
major only in its disposable pinned container.

Build each package twice from fresh environments with identical inputs and
compare both the archive digest and unpacked `BUILD-MANIFEST`. A mismatch blocks
release until explained and either eliminated or explicitly incorporated into
the reproducible input record. Sign the final archive and checksum file using
the release signing process; checksum generation alone is not provenance.
The release-candidate and tag-triggered draft workflows provide an initial
same-environment reproducibility gate with
`scripts/ci/check-release-artifacts.sh` and records the immutable build inputs
in `RELEASE-EVIDENCE.md`. The separate fresh-environment rehearsal remains a
release-approval requirement.

## Dependency, license, and vulnerability review

The current [RPM review record](rpm-dependency-review.md) records owner-reported
completion of dependency/security and license review and a scoped Rocky
exception. Durable retention and final release gates remain open; it is not a
clean security attestation or signed release approval.

Perform and record a review for every release. This section defines the
procedure and does **not** assert that a current review has been completed.

1. Inventory direct and transitive build/runtime dependencies from the build
   environment, `pkg-config`, the package manager, and `ldd` on the staged
   module. Reconcile that inventory with ADR 0001. Confirm that libjwt and
   Jansson are statically embedded and that the runtime OpenSSL and libcurl are
   the intended vendor-supported builds.
2. Verify the libjwt and Jansson commits and archive digests in
   `scripts/ci/install-libjwt.sh` and `scripts/ci/install-jansson.sh`; review
   upstream changes since the previously approved revisions. Re-run the
   dependency spike, strict boundary tests, sanitizers, and fuzz smoke before
   accepting an update.
3. Record versions and licenses for PostgreSQL, libjwt, Jansson, OpenSSL,
   libcurl, and all packaged transitive components. Review license texts and
   notice/source-offer obligations with the packaging owner. An unknown,
   incompatible, or missing license blocks publication.
   Confirm that `LICENSE`, `THIRD-PARTY-NOTICES.md`,
   `THIRD-PARTY-LICENSES/JANSSON`, and `THIRD-PARTY-LICENSES/LIBJWT-MPL-2.0` are
   present in each archive and listed in its manifest, and that the archive-path
   table in `THIRD-PARTY-NOTICES.md` still matches what the archive contains.
4. Query the operating-system vendor security tracker and the official
   upstream advisories for every dependency/version, recording query date,
   identifiers, applicability, and disposition. A scanner result is supporting
   evidence, not a substitute for applicability review.
5. Review GitHub dependency/security alerts and CI secret/code-scanning results
   for the tagged commit. Do not waive a finding without a named reviewer,
   rationale, expiry, and follow-up issue.

An exploitable unpatched vulnerability in the authentication path, an
unsupported OpenSSL/libcurl build, a dependency-integrity mismatch, or an
unresolved license obligation blocks release.

## Installation, upgrade, and rollback rehearsal

Use disposable staging instances representative of each supported major:

1. Install the package into a clean server and run the installed-path
   integration suite. Confirm package contents, ownership, permissions,
   dynamic dependencies, callback symbol, and optional LLVM bitcode.
2. Configure a mapped low-privilege identity and a protected non-OAuth recovery
   path. Prove mapped access succeeds and wrong issuer, audience, scope, token
   type, unmapped role, and administrative-role attempts fail closed without
   credential disclosure.
3. Upgrade from the previous supported package and configuration. Restart the
   server, because replacing the shared library is not reload-safe. Exercise
   provider availability, key rotation, cache freshness, and configuration
   reload behavior, then inspect protected server and client logs.
4. Restore the retained previous package and its matching configuration as one
   unit, restart, and repeat positive, negative, and redaction probes. Compare
   restored file digests with the recorded rollback artifact.
5. Rehearse operational recovery from an unavailable provider and from an
   invalid validator configuration using only the protected recovery path.

Do not recover by weakening issuer, audience, algorithm, token-type, scope,
TLS, or usermap policy. Keep OAuth authentication failed closed until the
package or configuration is restored and verified.

## PostgreSQL 19 prerelease-to-final gates

PostgreSQL 19 support is preview-only while PostgreSQL 19 is prerelease. After
the final PostgreSQL 19 source and packages are published, all of the following
must happen before removing the preview label:

- replace prerelease image/package pins with the exact final release and
  record their immutable digests;
- rebuild from clean state against final server and libpq headers and verify
  the exact `pg_config --version` in the package manifest;
- review the final OAuth validator callback, custom HBA option, memory/error,
  shared-memory, and PGXS contracts against PostgreSQL 19 source and release
  notes, including every change since the last tested prerelease;
- run all unit, component, integration, sanitizer, static-analysis, fuzz-smoke,
  package, installed-path, TLS, robustness, and provider-interoperability gates;
- repeat clean-install, PostgreSQL 18 coexistence, upgrade, and rollback
  rehearsals with the final PostgreSQL 19 artifact;
- update `FEATURES.md`, operator compatibility documentation, and release notes
  only after review approves the evidence.

A prerelease-built module must never be relabeled or shipped as a final
PostgreSQL 19 module.

## Blocking and publication criteria

Block publication for any required failing, unexpectedly skipped, flaky, or
unreproducible check; an unexplained artifact difference; missing provenance or
signature; a secret-scanning finding; an unresolved high-impact security or
license issue; failed install/upgrade/rollback rehearsal; incomplete PG-major
separation; credential disclosure; or any path that authorizes on uncertainty.

When blocked, retain the candidate and evidence, stop promotion, identify the
last known-good version, and open a tracked corrective action. Fix the source
or build input, create a new version/tag and fresh artifacts, and rerun every
affected gate. Never overwrite the rejected archive, checksum, signature, or
tag, and never normalize rerunning a flaky check until it happens to pass.

For a `0.x` prerelease, the maintainer may self-review and approve publication.
Record the reviewed commit, final diff inspection, security-relevant changes,
test and CI results, known risks, and approval in the release record. A second
person is welcome but not required. Before publishing `v1.0.0` or any later
release, obtain independent security-sensitive review and release approval from
someone other than the author.

Publication also requires successful signature verification and completion of
the per-major evidence above. Promote PostgreSQL 18 and PostgreSQL 19 artifacts
independently; a blocked preview artifact must not prevent a separately sound
PostgreSQL 18 release unless they share the defect or release version contract.
