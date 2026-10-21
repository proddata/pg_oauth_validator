# ADR 0006: SELinux confinement evidence in a disposable VM

- Status: Experimental test infrastructure; enforcing CI evidence required
- Date: 2026-10-21
- Scope: Fedora 44, PostgreSQL 18, x86-64

## Decision

Run the installed-RPM OAuth suite in a disposable KVM guest with its own Linux
kernel and SELinux policy. Containers remain useful for the distribution and
architecture matrix, but cannot establish this confinement boundary. Pin the
Fedora Cloud image by SHA-256. Record the guest kernel and installed policy
versions; mutable package repositories still prevent a reproducible-release
claim.

Reuse the existing offline, unprivileged RPM build and normal dependency-checked
installation gate. Export only its tested baseline artifact into the disposable
guest. The confinement phase installs that artifact without replacing its module
with `make install`. The shared-cache probe remains separate test instrumentation.

Opt into the confined fixture launcher explicitly. Use transient systemd services
with `SELinuxContext=system_u:system_r:postgresql_t:s0`, and verify the live
postmaster's context after readiness. Require global enforcing mode and reject a
per-domain permissive exception before and after startup. A missing tool, failed
launch, or unexpected domain fails the gate. Keep the existing unconfined fixture
path for ordinary container tests.

Label only private test database trees and temporary listening ports. Preserve
distribution labels on installed binaries and libraries. Route PostgreSQL stderr
to the unit's journal and reuse the existing diagnostic/redaction assertions on
that journal. Synchronize journal writes before reading it. This avoids granting
systemd additional file access merely to open a fixture's private log path.

Explicitly enable auditing in the disposable cloud guest. Temporarily disable
`dontaudit` suppression to expose diagnostic denials and restore it on exit;
this changes logging, not access permissions. Remove the cloud image's explicit
`never,task` audit rule so newly created tasks can produce evidence. Check the
lost-record counter and an explicit write/read marker. Select auditd's files
with `ausearch --input-logs`; a non-interactive invocation otherwise reads stdin.
An empty query result alone is not evidence of allowed access.

Do not disable enforcement, make PostgreSQL permissive, or generate an allow
module blindly from audit logs. Investigate actual AVC evidence before adding
any narrowly scoped, separately reviewed network permission. An absent required
permission must leave authentication denied; it cannot justify a validator
fallback. No operator policy is loaded by the RPM or its scriptlets.

Provide an optional CIL document with one permission: `postgresql_t` may
`name_connect` to TCP ports labeled `http_port_t`. The kernel socket trace in
[diagnostic run 39639693821](https://github.com/proddata/pg_oauth_validator/actions/runs/39639693821)
showed `EACCES` for IPv4 and IPv6 provider connections while the postmaster was
confined; its audit-rule listing exposed the suppressing `never,task` rule.
The permanent gate must produce a fresh matching enforcing AVC before installing
the module, run the complete suite with it, then remove it and prove denial in a
fresh cluster. An unexpected failure stage or missing evidence stops the gate
before any grant is loaded.

This grant affects the entire PostgreSQL domain and every port of that type,
including HTTP ports. SELinux does not distinguish this library or enforce its
trusted hostname/TLS policy. Keep those application checks and require explicit
operator review of other extensions, instances, and egress controls. Do not
enable broad networking booleans or grant unrelated permissions. The shipped
text is RPM-owned documentation; loading it is a separate administrator action.
Removal affects future network access, while cached-key behavior remains governed
by ADR 0002. The absence/removal tests use fresh clusters to avoid masking denial.

## Limits

The full local-provider suite exercises authorization, negative cases, TLS,
reloads, cache behavior, rotation, outages, and redaction under this domain.
It does not establish Enterprise Linux or ARM64 SELinux compatibility, public
provider interoperability, release-specific binary upgrade compatibility, or
production readiness. Guest credentials, provider keys, token files, and raw
PostgreSQL logs are not published as artifacts.

## References

- [systemd.exec: mandatory access control](https://www.freedesktop.org/software/systemd/man/latest/systemd.exec.html#SELinuxContext=)
  documents the explicit context and the continued requirement for policy to
  authorize a transition.
- [QEMU invocation](https://www.qemu.org/docs/master/system/invocation.html)
  documents KVM acceleration and loopback-bound SSH forwarding.
- [Fedora 44 PostgreSQL SELinux policy](https://github.com/fedora-selinux/selinux-policy/blob/f44/policy/modules/services/postgresql.te)
  defines the distribution domain and its existing resource permissions.
- [ADR 0005](0005-experimental-rpm-packaging.md) describes the RPM artifact and
  offline build boundary.

- [auditctl: disabled by default](https://github.com/linux-audit/audit-userspace/blob/master/docs/auditctl.8)
  describes the `never,task` rule and per-task audit suppression.
- [SELinux CIL access-vector rules](https://github.com/SELinuxProject/selinux/blob/main/secilc/docs/cil_access_vector_rules.md)
  documents the explicit single-permission grant.

- [ausearch input selection](https://github.com/linux-audit/audit-userspace/blob/master/docs/ausearch.8)
  documents `--input-logs` for non-interactive log searches.
