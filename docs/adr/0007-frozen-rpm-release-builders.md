# ADR 0007: Retained RPM builders and real-version rehearsals

- Status: Release tooling; artifact and dependency review required
- Date: 2026-10-22
- Scope: PostgreSQL 18, Fedora 43/44 and Rocky Linux 10, x86-64 and ARM64

## Decision

Separate online builder capture from offline release compilation. Capture the
complete Docker image with all parent layers, embedded dependency sources,
package versions/licenses/source-RPM identities, compiler/linker/PostgreSQL
versions, image configuration, base-image digest and capture-source revision.
Bind the archive and evidence files to SHA-256 and exact sizes in a reviewed
six-target lock. Image labels must match the capture source and base digest.

Retain each archive using an immutable GitHub Actions artifact ID and workflow
run ID. A lock update is a release-input change requiring review. Loading a
builder verifies every retained file before Docker parses the archive, then
checks its image ID, native architecture, Linux OS and source/base labels.
Run containers by the verified image ID with pulling disabled. Missing,
expired, corrupt, incomplete or mismatched inputs fail; never rebuild from
current repositories as a fallback.

The capture operation may resolve mutable repositories. Once reviewed, the
complete archived image is the immutable release input; this does not claim
that a future online reconstruction of the image will resolve identical RPMs.
Two fresh CI runners must load that same retained archive and build/test the
candidate offline. Compare complete unsigned RPM/SRPM bundles, not only the
module payload. Compatibility CI may continue to discover repository changes
separately; it is not the release-input authority.

Capture is explicit: manual dispatch on main, or opening/reopening a capture
pull request. Ordinary pushes do not silently replace an already captured
builder. Initial capture records name the capture commit, independently of
later commits that consume the lock. All image layers are retained, so capture
must never receive production credentials or copy checkout authentication into
the image. The Containerfile copies only explicit public build/test inputs.

Keep archives for 90 days in CI and transfer approved builder/candidate/evidence
bundles to durable release storage before expiry. CI checksums and artifact IDs
are integrity records, not release signatures or vulnerability approval. Review
the complete runtime dependency inventory and upstream/vendor advisories before
approving a builder; freeze a new input set for relevant security updates.

## Versioned rehearsal

Prepare the previous release and candidate from distinct exact source commits,
with independently recorded versions and source digests. Retain the actual
generated RPMs before performing A -> B -> A. Reuse the existing stopped-server,
retained-cluster, configuration-pair restoration and digest checks. Rehearsal
must consume retained package files, not rebuild a replacement during rollback.

The first RPM baseline is rebuilt from v0.1.2's exact source: that release had
no published RPMs. Record this explicitly; it is source-release migration
evidence, not compatibility with a previously published RPM. The provisional
candidate is 0.1.3 and remains unsigned/unpublished until release approval.

The existing Fedora 44 x86-64 enforcing VM evidence remains the SELinux gate
for this work, as requested by the maintainer. Record its scope; do not claim
equivalent enforcing tests ran on all six targets. This changes no installed
policy, application trust boundary, provider profile or authentication default.

## References

- [Docker image save](https://docs.docker.com/reference/cli/docker/image/save/)
  documents retention of parent layers.
- [Docker image load](https://docs.docker.com/reference/cli/docker/image/load/)
  documents restoration of archived images.
- [Artifact immutability](https://github.com/actions/upload-artifact#v4---whats-new)
  describes immutable artifact IDs; retention still limits availability.
- [ADR 0005](0005-experimental-rpm-packaging.md) and
  [release readiness](../release-readiness.md) define package and approval gates.
