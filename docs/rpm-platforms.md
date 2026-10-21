# RPM-family platform work

Fedora and Enterprise Linux support is experimental. The current release
contract remains the pinned Debian/PGDG environment documented in
[`release-readiness.md`](release-readiness.md). A successful compatibility
build does not make an RPM-family platform a supported production target.

## Compatibility matrix

| Environment | PostgreSQL | Architecture | Current gate |
| --- | --- | --- | --- |
| Fedora 43 | Fedora PostgreSQL 18 | x86-64 | strict build, unit/component verification, installed OAuth integration |
| Fedora 43 | Fedora PostgreSQL 18 | arm64 | strict build, unit/component verification, installed OAuth integration |
| Fedora 44 | Fedora PostgreSQL 18 | x86-64 | strict build, unit/component verification, installed OAuth integration |
| Fedora 44 | Fedora PostgreSQL 18 | arm64 | strict build, unit/component verification, installed OAuth integration |
| Rocky Linux 10 | PGDG PostgreSQL 18 | x86-64 | strict build, unit/component verification, installed OAuth integration |
| Rocky Linux 10 | PGDG PostgreSQL 18 | arm64 | strict build, unit/component verification, installed OAuth integration |

CI uses native `ubuntu-24.04` and `ubuntu-24.04-arm` runners for the respective
architectures. The pinned Fedora and Rocky base-image indexes include both
architectures. Every matrix job builds its own image and runs `verify-dev` and
`installedcheck`. The latter checks staged contents, installs the module into
the distribution's PostgreSQL module directory, and runs the complete local
real-server integration suite against that installed library. The local
Compose services use separate build volumes for x86-64 and arm64.
The same arm architecture is called `aarch64` by RPM packages.

Each matrix cell compiles against that distribution's PostgreSQL headers and
system libraries on the target CPU. These builds produce disposable test RPMs,
not release artifacts. Contributors can run one local cell while
developing, then rely on CI for the full matrix. The main CI workflow separately
covers the supported Debian target.

Fedora uses `/usr/bin/pg_server_config` for server extension builds. The PGDG
Enterprise Linux packages use `/usr/pgsql-18/bin/pg_config`. The same source
build is exercised through the Makefile's explicit `PG_CONFIG` boundary, but
artifacts are not interchangeable across distributions or architectures.

Fedora's PostgreSQL server-development package conflicts with the standalone
`libpq-devel` package. The test image uses the `postgresql-private-devel` and
`postgresql-private-libs` dependencies supplied with the server-development
package. These provide the PostgreSQL 18 client headers and `libpq.so` needed
by the OAuth test client without installing the conflicting standalone package.
The module and client both use paths reported by `pg_server_config`; the suite
exercises the resulting private libpq through real OAuth connections.

Rocky Linux 9's libcurl 7.76.1 is below the required 7.87.0 runtime minimum.
Rocky Linux 10 provides a current system libcurl; the runtime test still checks
its version and asynchronous DNS capability. The Rocky build enables CRB for
PGDG development dependencies. The PGDG repository bootstrap RPM is versioned
and SHA-256 verified for each architecture before installation; repository and
package metadata signatures remain enforced.

Run the compatibility builds with:

```sh
docker compose -f compose.rpm-test.yml build
docker compose -f compose.rpm-test.yml up --abort-on-container-failure
```

These default services build and test x86-64. On an arm64 host, use the native
arm64 services instead:

```sh
docker compose -f compose.rpm-test.yml build fedora43-arm64 fedora44-arm64 rocky10-arm64
docker compose -f compose.rpm-test.yml up --abort-on-container-failure fedora43-arm64 fedora44-arm64 rocky10-arm64
```

Running the arm64 services on an x86-64 host requires container emulation and
does not provide the native-runner evidence used by CI.

The base-image manifests are pinned. Package repositories are not yet
snapshot-pinned, so these jobs are compatibility signals rather than
reproducible release builders. `/etc/pg-oauth-build-inputs` in each image
records the RPM architecture and selected compiler, PostgreSQL, OpenSSL, and
libcurl packages.

The installed-runtime gate runs only in disposable containers: `installedcheck`
writes into the PostgreSQL installation and starts temporary clusters as the
`postgres` user. It exercises accepted and rejected OAuth connections, TLS
verification, direct/usermap/delegated role authorization, key rotation,
provider outage and recovery, configuration reload, concurrency, and token
redaction. PostgreSQL-19-only custom-HBA-option tests are expected skips on
these PostgreSQL 18 images. The suite does not require a live identity provider.
Container results do not establish SELinux confinement as `postgresql_t`;
the enforcing-VM investigation below remains required.

Each temporary cluster uses its own protected socket directory for
administrative test connections, independent of distribution defaults or
systemd-created directories. OAuth probes still connect over TCP/TLS and the
configured OAuth HBA rule. The harness disables the logging collector and sends
diagnostics to the protected `pg_ctl` log so denial and redaction assertions
inspect the actual server output on every distribution.

Local verification on 2026-10-20 used native arm64 Docker builds of all three
distributions. Each service's default `verify-dev installedcheck` passed,
including **28 passed, 2 expected PostgreSQL-19-only skips** in the installed
integration suite. The updated harness also passed the Debian installed suites
on PostgreSQL 18 (**28 passed, 2 skips**) and 19 RC1 (**30 passed**). These local
results do not establish x86-64 CI results, RPM package lifecycle behavior, or
SELinux enforcement.

Fedora 42 reached end of life in May 2026 and its distribution PostgreSQL is
older than the supported majors. It is not a continuous CI target; any
historical build against archived PGDG PostgreSQL 18 packages is a separate,
opt-in compatibility investigation and cannot establish current support.

## Offline RPM packaging

The experimental [spec template](../packaging/rpm/pg_oauth_validator.spec.in)
builds `postgresql18-pg_oauth_validator` for PostgreSQL **18.6 or newer within
major 18**. Fedora uses `postgresql-server` plus its `MODULE_COMPAT_18` ABI
capability; Enterprise Linux uses `postgresql18-server`. Together with the
architecture-qualified version bounds, this excludes PostgreSQL 19 prereleases
as well as final releases. PostgreSQL 19 packaging is not implemented. See
[ADR 0005](adr/0005-experimental-rpm-packaging.md).

The image build fetches and retains the reviewed Jansson/libjwt source archives
under `/opt/rpm-dependencies`, verifies their existing installer pins, and
installs the RPM build tools. Preparation and `rpmbuild` then run without
networking. Source0 is an archive of an explicit full Git commit, so uncommitted
changes and generated files are excluded. The spec is generated from the current
template as a separate input: `INPUTS.sha256` records its digest independently
from that source commit. Only `MAJOR.MINOR.PATCH` versions are currently accepted;
CI uses disposable version `0.0.0`.

To exercise one native arm64 cell after building its image:

```sh
python3 tests/ci/test_rpm_sources.py
mkdir -p dist
docker run --rm --network=none --entrypoint python3 \
  -v "$PWD:/workspace:ro" -v "$PWD/dist:/output" \
  pg-oauth-validator-rpm-test:fedora44-arm64 \
  /workspace/scripts/ci/prepare-rpm-sources.py prepare \
  0.0.0 "$(git rev-parse HEAD)" /opt/rpm-dependencies /output/rpm-inputs

docker run --rm --network=none --tmpfs /source:exec --entrypoint /bin/sh \
  -v "$PWD:/workspace:ro" -v "$PWD/dist/rpm-inputs:/inputs:ro" \
  pg-oauth-validator-rpm-test:fedora44-arm64 \
  /workspace/scripts/ci/test-rpm-package.sh /inputs
```

Use the corresponding image for Fedora 43 or Rocky 10; on x86-64 use the image
name without `-arm64`. Preparation refuses to overwrite an existing input
directory. Review and retain it, or select a new output directory for a new run.
To fetch the reviewed archives separately, run
`python3 scripts/ci/prepare-rpm-sources.py fetch /path/to/dependencies`; fetching
is the only online operation and never substitutes an unreviewed archive.

The package gate builds as `nobody`, keeps the standard RPM hardening and
debug-package checks, verifies complete source archives in the SRPM, and installs
with normal dependency checking. It runs the full local OAuth suite against the
RPM-installed module, verifies package file digests, and removes the package.
The test container must have no active non-loopback interface. No package is
published. By default no artifact is retained; `RPM_TEST_EXPORT_DIR` may name
an existing directory in the disposable test environment to retain the exact
tested baseline as `validator.rpm` for the VM confinement gate.

During `rpmbuild`, both embedded libraries are rebuilt in a private prefix from
the supplied archives; neither compilation nor dependency testing may reuse the
image's `/usr/local` libraries. Embedded-library LTO is disabled to keep
third-party link-time diagnostics outside the project's fatal-warning boundary;
PGXS retains its normal LTO settings for project code. RPM's debug tools handle
project debug paths. PGDG's unnecessary private-library RPATH is disabled through
PGXS, without bypassing RPM's RPATH check. The compatibility image's ordinary
build gate still uses its reviewed `/usr/local` prefix.

The binary RPM includes complete project/Jansson/libjwt licenses, bundled-library
metadata, operator documentation, optional PGXS bitcode, and `BUILD-SOURCE`
with source digests, revision, PostgreSQL version, and build-package inventory.
The SRPM carries all source archives and the generated spec. These unsigned
artifacts are not release provenance or evidence of reproducible binary RPMs.

Installation has no scriptlets: it does not edit HBA, configure trusted issuers,
create roles, or restart PostgreSQL. Before replacing or removing an enabled
module, arrange the protected recovery path described in
[`operations.md`](operations.md). Upgrades and rollback must restore the matching
package and configuration together and restart PostgreSQL; replacing a loaded
shared library is not reload-safe. Release-specific upgrade/rollback rehearsals
and SELinux tests remain separate requirements.

On 2026-10-20 the offline package gate passed on native arm64 Fedora 43,
Fedora 44, and Rocky Linux 10, including strict unit/component verification,
complete SRPM sources, RPM metadata/ABI/license/linkage checks, installation,
**28 passed and 2 expected skips** per installed OAuth suite, file verification,
and removal. Source0 was commit
`1dc4bdeab693e4f414e91c803e2070aaec7630c5`; the generated development spec was a
separately hashed working-tree input. Seven deterministic source-preparation
tests passed, including corrupt/missing archives, exact-commit exclusion of
working-tree edits, and verified download publication. External-network and
unsupported-distribution rejection probes also passed. x86-64 CI and
SELinux-enforcing VM checks were not run locally.

## Upgrade and rollback rehearsal

The six-cell CI matrix additionally prepares disposable versions `0.0.0` (A)
and `0.0.1` (B) from its exact Git commit. Both packages pass the same offline
build, complete-source, dependency/ABI, scriptlet, and licensing checks. The
existing full installed-security suite runs on A before the rehearsal.

The rehearsal then keeps one PostgreSQL data directory and performs:

1. Start A with a single test audience and a mapped low-privilege role. Save the
   exact configuration bytes, permissions, and ownership, plus the retained RPM
   and installed-module SHA-256. Create a marker row to detect data replacement.
2. Stop PostgreSQL cleanly with bounded `pg_ctl -m fast -w`, prove it is stopped,
   upgrade to B with normal RPM dependency/conflict checking, and verify package
   identity, payload digests, ownership, and source-version record. The package
   transaction must leave operator configuration unchanged.
3. Install B's test configuration with a different exact audience and start the
   same cluster. Check a new postmaster start time and the retained marker row.
4. Stop again. Prove an implicit downgrade is rejected without changing B, then
   explicitly restore the retained A RPM with `rpm --upgrade --oldpackage`.
   Verify the original module digest, restore all saved configuration files and
   their ownership/permissions, and start the same cluster again.

Every phase proves `verify-full` TLS authentication and audited identity, and
rejects the other version's audience, wrong issuer, missing required scope,
expired/wrong-type/malformed/bad-signature tokens, unmapped principals, and
administrative-role requests. It also exercises signing-key replacement,
provider failure, the protected local recovery connection, provider recovery,
and redaction of tokens, signatures, private claims, and provider error bodies.
Zero advertised provider freshness makes the outage probes deterministic without
waiting for expiry. No issuer, scope, TLS, algorithm, or usermap policy is weakened
to recover access.

For a local run, prepare B into a new directory alongside the A inputs above:

```sh
docker run --rm --network=none --entrypoint python3 \
  -v "$PWD:/workspace:ro" -v "$PWD/dist:/output" \
  pg-oauth-validator-rpm-test:fedora44-arm64 \
  /workspace/scripts/ci/prepare-rpm-sources.py prepare \
  0.0.1 "$(git rev-parse HEAD)" /opt/rpm-dependencies /output/rpm-candidate

docker run --rm --network=none --tmpfs /source:exec --entrypoint /bin/sh \
  -v "$PWD:/workspace:ro" -v "$PWD/dist/rpm-inputs:/inputs:ro" \
  -v "$PWD/dist/rpm-candidate:/candidate:ro" \
  pg-oauth-validator-rpm-test:fedora44-arm64 \
  /workspace/scripts/ci/test-rpm-package.sh /inputs /candidate

python3 tests/ci/test_rpm_upgrade.py
```

The second input directory is optional; omitting it retains the single-package
install/authenticate/remove gate. The four deterministic configuration-recovery
tests cover exact restoration. Capture rejects missing, oversized, or symlinked
files; restoration rejects unexpected paths and symlinks, and audience changes
reject ambiguous substitution.

This is a transaction/restart/configuration-pair smoke test using two versions
of the same source, not compatibility evidence between different releases. A
release must repeat it with the retained previous artifact, actual candidate,
and reviewed migration configuration. Same-commit CI cannot prove compatibility
with changed defaults, dependency versions, or shared-memory layouts. SELinux
confinement remains a separate enforcing-VM requirement.

Local verification on 2026-10-21 passed the full installed suite (**28 passed,
2 expected PostgreSQL-19-only skips**) and A -> B -> A rehearsal on native
arm64 Fedora 43, Fedora 44, and Rocky Linux 10. Both disposable packages used
source commit `40eb537294adfe61bc788ee368b1f8dc2f354096`, with versions `0.0.0`
and `0.0.1`. Four configuration-recovery tests, the seven source-preparation
tests, source hygiene, and formatting passed. The one-input gate also passed
after the build-function refactor. Updated x86-64 CI and SELinux-enforcing VM
tests were not run locally.

## Required work before support

An RPM-family platform becomes supported only after all of the following are
complete for an exact distribution release and architecture:

- an offline RPM build with immutable source inputs and a reviewed spec;
- exact PostgreSQL-major package dependencies and installation paths;
- clean install, upgrade, rollback, removal, and installed-path tests;
- runtime dependency, symbol, permission, license, and static-link checks;
- real PostgreSQL OAuth integration tests under SELinux enforcing mode;
- provider outage, key rotation, configuration reload, and redaction tests;
- a reviewed vulnerability inventory and reproducible artifact evidence.

## SELinux investigation

Do not disable SELinux, make `postgresql_t` permissive, or install policy
generated blindly from `audit2allow`. First install the candidate RPM on a
disposable Fedora or Enterprise Linux virtual machine, confirm `getenforce`
reports `Enforcing`, and run the complete local-HTTPS integration suite while
PostgreSQL is confined as `postgresql_t`.

Review recent denials with:

```sh
ausearch --input-logs -m AVC,USER_AVC -ts recent
```

If the distribution policy blocks required discovery or JWKS traffic, add the
smallest reviewed policy module that permits the necessary DNS resolution, CA
bundle access, and connection to the configured HTTPS port type. SELinux
policy can constrain resource and port types; the validator must continue to
enforce the exact administrator-configured issuer host, TLS verification, and
redirect/proxy prohibitions. Absence or failure of required policy must remain
a deterministic authentication denial.

### Disposable enforcing VM gate

The `SELinux enforcing VM` workflow boots the Fedora 44 x86-64 Cloud image
in a disposable KVM guest. The image URL and SHA-256 are fixed in
`scripts/ci/test-selinux-vm.sh`; the digest comes from Fedora's signed
[44-1.7 Cloud checksum manifest](https://download.fedoraproject.org/pub/fedora/linux/releases/44/Cloud/x86_64/images/Fedora-Cloud-44-1.7-x86_64-CHECKSUM).
Guest package repositories remain mutable, as in the RPM compatibility jobs.
The host requires Linux, writable `/dev/kvm`, QEMU, `cloud-localds`, curl,
OpenSSH, and tar. Run from a clean checkout with no persisted Git credentials:

```sh
sh scripts/ci/test-selinux-vm.sh
```

The root-only guest script installs build dependencies, runs the existing
network-isolated RPM build/install/removal gate, and retains its tested RPM.
It installs that artifact normally, then runs the complete integration suite
with `OAUTH_TEST_SELINUX=1`. Each temporary cluster lives under `/var/lib/pgsql`;
its systemd unit explicitly starts the packaged PostgreSQL binary in
`system_u:system_r:postgresql_t:s0`. Every successful startup checks the live
postmaster's `/proc/PID/attr/current`, global enforcement, and absence of a
per-domain permissive exception. Missing tools and confinement failures fail
the test. The normal integration path does not use this launcher.

Only temporary fixture files and ports receive test labels: database content
uses `postgresql_db_t`, PostgreSQL listener ports
use `postgresql_port_t`, and local provider ports use `http_port_t`. Tests
remove their port overrides and systemd units. Installed binaries and modules
retain distribution labels. Confined services send stderr to journald; the
existing diagnostic/redaction assertions read only their specific unit's journal.
The cache probe is separate test instrumentation.
The disposable guest explicitly enables kernel auditing and temporarily disables
`dontaudit` suppression for diagnosis, restoring suppression on exit. It removes
only the image's explicit `never,task` audit rule, which otherwise suppresses new
tasks' audit processing even with auditing enabled. This changes
audit visibility without granting access or relaxing enforcement. The guest
prints AVC evidence on failure; it never uploads bearer-token files,
provider keys, or raw PostgreSQL logs as artifacts. This gate initially tests
the distribution policy without an added grant, requires a valid token to be
denied with a fresh `name_connect` AVC to the provider's `http_port_t` port,
then installs the optional policy below and runs the full suite. It removes the
module and repeats the denial check in a fresh cluster, then verifies the RPM
payload and that the kernel lost no audit records. Fresh clusters prevent cached
keys from masking removal. Enterprise Linux and ARM64 VM coverage remain separate
work.


### Optional HTTP-port policy

The shipped example `docs/selinux/pg_oauth_https.cil` adds exactly one permission:
`postgresql_t` may initiate TCP connections to ports labeled `http_port_t`.
Installation of the RPM does not load this policy. After reviewing it against
the host's requirements, an administrator can load the RPM-owned copy:

```sh
semodule -i /usr/share/doc/postgresql18-pg_oauth_validator/docs/selinux/pg_oauth_https.cil
```

This permission applies to **every process in `postgresql_t`**, including other
PostgreSQL instances and extensions, and **every port of that type**. The type
also includes HTTP ports; it does not enforce TLS, a hostname, or an individual
library. The validator continues to enforce its configured issuer host, HTTPS,
certificate verification, and redirect/proxy prohibitions. Review other code
running in this domain and any host egress controls. Inspect custom provider
port labels before changing them; do not overwrite another service's mapping
blindly. This example does not grant DNS, general filesystem, arbitrary-port,
or unconfined-domain access.

To remove the added permission:

```sh
semodule -r pg_oauth_https
```

Removal prevents the additional network access; it does not revoke keys already
in the validator's cache. Follow the documented cache/outage policy, or restart
the server when immediate cache invalidation is required. An absent, rejected,
or removed module must leave a fresh validator unable to retrieve keys; the VM
regression checks this denial and secret redaction before installation and after
removal. Kernel/audit visibility changes in the disposable test VM are diagnostic
steps, not operator prerequisites for the module.
