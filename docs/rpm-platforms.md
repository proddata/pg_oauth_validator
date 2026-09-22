# RPM-family platform work

Fedora and Enterprise Linux support is experimental. The current release
contract remains the pinned Debian/PGDG environment documented in
[`release-readiness.md`](release-readiness.md). A successful compatibility
build does not make an RPM-family platform a supported production target.

## Initial compatibility matrix

| Environment | PostgreSQL | Architecture | Current gate |
| --- | --- | --- | --- |
| Fedora 44 | Fedora PostgreSQL 18 | x86-64 | strict build, unit/component verification, staged installation |
| Rocky Linux 9 | PGDG PostgreSQL 18 | x86-64 | strict build, unit/component verification, staged installation |

Fedora uses `/usr/bin/pg_server_config` for server extension builds. The PGDG
Enterprise Linux packages use `/usr/pgsql-18/bin/pg_config`. The same source
build is exercised through the Makefile's explicit `PG_CONFIG` boundary, but
artifacts are not interchangeable across the two environments.

Fedora's PostgreSQL server-development package conflicts with the standalone
`libpq-devel` package. The compatibility image intentionally installs the
server-private development interface needed by PGXS and does not claim that
the client-side OAuth integration suite is available there. Resolving that
split belongs to the later installed-runtime test image.

The Rocky build enables the distribution's CRB repository because PGDG's
`postgresql18-devel` package requires `perl-IPC-Run`, which is not supplied by
the minimal BaseOS/AppStream repository set. The PGDG repository bootstrap RPM
is versioned and SHA-256 verified before installation; repository and package
metadata signatures remain enforced.

Run both compatibility builds with:

```sh
docker compose -f compose.rpm-test.yml build
docker compose -f compose.rpm-test.yml up --abort-on-container-failure
```

The base-image manifests are pinned. Package repositories are not yet
snapshot-pinned, so these jobs are compatibility signals rather than
reproducible release builders. `/etc/pg-oauth-build-inputs` in each image
records the selected compiler, PostgreSQL, OpenSSL, and libcurl packages.

The Jansson and libjwt installers accept pre-fetched archives through
`JANSSON_SOURCE_ARCHIVE` and `LIBJWT_SOURCE_ARCHIVE`. They verify the same
reviewed SHA-256 values used for network retrieval and perform no download when
an archive is supplied. A future RPM build must provide those archives as
immutable RPM sources; network access during `rpmbuild` is not permitted.
The RPM-family image sets `PKG_CONFIG_PATH` explicitly to the reviewed
`/usr/local` static dependency prefix; Enterprise Linux does not search that
prefix by default.

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
ausearch -m AVC,USER_AVC -ts recent
```

If the distribution policy blocks required discovery or JWKS traffic, add the
smallest reviewed policy module that permits the necessary DNS resolution, CA
bundle access, and connection to the configured HTTPS port type. SELinux
policy can constrain resource and port types; the validator must continue to
enforce the exact administrator-configured issuer host, TLS verification, and
redirect/proxy prohibitions. Absence or failure of required policy must remain
a deterministic authentication denial.
