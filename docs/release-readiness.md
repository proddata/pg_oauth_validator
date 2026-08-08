# Release readiness

This checklist defines the evidence required to publish a
`pg_oauth_validator` release. Passing it means a particular source revision and
its version-specific packages were reviewed and rehearsed; it does not by
itself claim that the project or every deployment is production-ready.

## Version and tag policy

- Use semantic versions of the form `MAJOR.MINOR.PATCH`. Use a SemVer
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

## Release record

Create an immutable release record containing:

- tag, full source commit, source-tree status, release version, and
  `SOURCE_DATE_EPOCH` (normally the tagged commit timestamp);
- build host/container image digests, compiler and linker versions, exact
  `pg_config --version`, architecture, and operating-system package versions;
- the dependency review described below;
- commands and results for every required check, including expected skips;
- archive names, archive SHA-256 files, and signatures;
- reviewer and release approver identities, known limitations, and rollback
  artifact identifiers.

The tree must contain no uncommitted or untracked release inputs. CI artifacts
are evidence only when they were built from the tagged commit with the same
pinned inputs.

## PostgreSQL-specific artifacts

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

## Dependency, license, and vulnerability review

Perform and record a review for every release. This section defines the
procedure and does **not** assert that a current review has been completed.

1. Inventory direct and transitive build/runtime dependencies from the build
   environment, `pkg-config`, the package manager, and `ldd` on the staged
   module. Reconcile that inventory with ADR 0001. Confirm that libjwt and
   Jansson are statically embedded and that the runtime OpenSSL and libcurl are
   the intended vendor-supported builds.
2. Verify the libjwt commit and archive digest in
   `scripts/ci/install-libjwt.sh`; review upstream changes since the previously
   approved revision. Re-run the dependency spike, strict boundary tests,
   sanitizers, and fuzz smoke before accepting an update.
3. Record versions and licenses for PostgreSQL, libjwt, Jansson, OpenSSL,
   libcurl, and all packaged transitive components. Review license texts and
   notice/source-offer obligations with the packaging owner. An unknown,
   incompatible, or missing license blocks publication.
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

## PostgreSQL 19 beta-to-final gates

PostgreSQL 19 support is preview-only while PostgreSQL 19 is prerelease. After
the final PostgreSQL 19 source and packages are published, all of the following
must happen before removing the preview label:

- replace beta image/package pins with the exact final release and record their
  immutable digests;
- rebuild from clean state against final server and libpq headers and verify
  the exact `pg_config --version` in the package manifest;
- review the final OAuth validator callback, custom HBA option, memory/error,
  shared-memory, and PGXS contracts against PostgreSQL 19 source and release
  notes, including every change since the last tested beta;
- run all unit, component, integration, sanitizer, static-analysis, fuzz-smoke,
  package, installed-path, TLS, robustness, and provider-interoperability gates;
- repeat clean-install, PostgreSQL 18 coexistence, upgrade, and rollback
  rehearsals with the final PostgreSQL 19 artifact;
- update `FEATURES.md`, operator compatibility documentation, and release notes
  only after review approves the evidence.

A beta-built module must never be relabeled or shipped as a final PostgreSQL 19
module.

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

Publication is allowed only after independent security-sensitive review,
release-owner approval, successful signature verification, and completion of
the per-major evidence above. Promote PostgreSQL 18 and PostgreSQL 19 artifacts
independently; a blocked preview artifact must not prevent a separately sound
PostgreSQL 18 release unless they share the defect or release version contract.
