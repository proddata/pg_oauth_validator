# ADR 0005: Experimental offline RPM packaging

- Status: Experimental implementation; release and platform approval required
- Date: 2026-10-20
- Scope: PostgreSQL 18 on Fedora 43/44 and Enterprise Linux 10, x86-64 and aarch64

## Decision

Build a distribution-specific `postgresql18-pg_oauth_validator` RPM and a complete
SRPM. The generated spec declares architecture-qualified PostgreSQL server
dependencies `>= 18.6` and `< 19`, matching the existing security-refresh
baseline. Fedora uses its server-private development interface; Enterprise Linux
uses PGDG's versioned installation. Distribution and architecture artifacts are
not interchangeable. PostgreSQL 19 packaging needs a separate reviewed spec.
Fedora additionally requires `postgresql-server(:MODULE_COMPAT_18)` to bind the
server ABI exactly; a version range alone could admit RPM's `19~beta` spelling.

Prepare Source0 from an explicit full Git commit, excluding working-tree edits
and generated files. Keep the generated spec separately hashed: a development
spec may differ from that commit and must not be attributed to it. Initially
accept only `MAJOR.MINOR.PATCH`; prerelease-to-RPM version ordering is deferred.
CI's `0.0.0` packages are disposable test artifacts, not release candidates.

Carry the exact Jansson and libjwt archives in the SRPM. Read their pins from
the selected commit's existing installers and verify them before packaging and
again before compilation. Fetching is a separate online operation. Offline
preparation fails on missing or corrupt inputs; `rpmbuild` runs as an
unprivileged user in a container with networking disabled. Compile the embedded
libraries into a private build prefix, excluding the image's `/usr/local`
libraries from dependency discovery. Keep OpenSSL and libcurl dynamically linked
to the distribution's packages, and declare both bundled components for audits.
Compile the embedded libraries without LTO, matching the existing dependency
installer baseline; preserve PGXS's distribution LTO and fatal warnings for
project code. Use RPM's normal debug-source handling and disable PGDG's
unnecessary private RPATH through PGXS rather than weakening RPM's checks.

Install only the module, optional PGXS bitcode, package-owned documentation, and
the complete project and embedded-library licenses. Include source revision,
source digests, PostgreSQL version, and installed build-package inventory in
`BUILD-SOURCE`; RPM records final payload digests. Do not install configuration,
add trust, provision roles, or run package scriptlets or service restarts.

## Evidence and limits

The packaging gate checks complete SRPM sources and hashes, package metadata,
major-version dependency bounds, scriptlet absence, license files, symbol and
dynamic linkage, file permissions, normal RPM installation, real OAuth
authentication against the installed RPM library, RPM file verification, and
removal. It must not overwrite that library with `make install` during testing.

The optional two-package gate rehearses A -> B -> A in one retained PostgreSQL
cluster. It stops the server cleanly before each package transaction, verifies
new process startup and retained data, changes the test audience for B, and
restores A's exact configuration and library digest on rollback. RPM's
`--oldpackage` is used only for the explicit rollback; dependency, architecture,
and conflict checks remain enabled. Rejection of an implicit downgrade is a
negative test. Authentication, denial, rotation, outage/recovery, and redaction
are checked in every phase over verified TLS.

CI's two package versions use the same source commit. This proves the operational
mechanism, not compatibility between releases. Release-specific rehearsals must
use retained real artifacts and reviewed configuration migration, and remain
required for release approval.

This is not a Fedora/EPEL submission or a platform-support approval. Repository
packages are not snapshot-pinned. Reproducible release builds, signing,
vulnerability review, release-specific upgrade/rollback rehearsal, and
SELinux-enforcing VM tests remain release requirements. Operators must restart PostgreSQL after replacing
or restoring its validator library; package transactions do not make an active
backend safely reload a changed module.

## References

- [RPM spec manual](https://rpm.org/docs/4.20.x/manual/spec.html): source entries,
  dependencies, build stages, scriptlets, and license/document file attributes.
- [PostgreSQL 18 PGXS](https://www.postgresql.org/docs/18/extend-pgxs.html):
  version-specific paths selected through `PG_CONFIG`.
- [ADR 0001](0001-jose-json-crypto-http-dependencies.md): reviewed embedded
  dependencies and system cryptography.
