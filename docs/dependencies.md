# Dependency versions and link modes

This is the reference for downstream distribution packagers. It states the
minimum version of every direct dependency, the source-level reason for each
floor, and how to choose between linking a reviewed static archive and linking
the deployment environment's shared library.

The project's own release contract is unchanged and is documented in
[`release-readiness.md`](release-readiness.md): a pinned Debian/PGDG
environment with both JOSE and JSON dependencies linked statically. Everything
below describes supported configurations for packaging this source tree
elsewhere, not additional supported production targets.

## Minimum versions

| Dependency | Minimum | Established by | Verified against |
| --- | --- | --- | --- |
| libjwt | 3.3.3 | The `jwt_checker_*` verification API used in `src/signature.c` exists only in libjwt 3.x. 3.3.3 is the reviewed floor in [ADR 0001](adr/0001-jose-json-crypto-http-dependencies.md) | 3.3.3 |
| Jansson | 2.7 | `json_string_length` (2.7); `json_loadb` and `JSON_REJECT_DUPLICATES` (2.1) | 2.14, 2.15.1 |
| libcurl | 7.63.0 | `CURLOPT_CURLU`, which takes a `CURLU` handle from the URL API (7.62.0), so the transport never re-parses a string URL | 8.x |
| OpenSSL | 3.0.0 | Supported-release policy in [ADR 0001](adr/0001-jose-json-crypto-http-dependencies.md) and [`../THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md). libcurl must use the OpenSSL backend | 3.x |

`make all` enforces every floor through `check-link-dependencies` before the
first object is compiled, so a too-old dependency is reported by name instead of
surfacing as a missing declaration or an unresolved symbol.

Two libcurl options are newer than the floor and are selected at compile time,
so no higher minimum is required:
`CURLOPT_PROTOCOLS_STR`/`CURLOPT_REDIR_PROTOCOLS_STR` (7.85.0) are used when
available and fall back to the equivalent bitmask options otherwise
(`src/http_transport.c`). The protocol restriction itself is not optional in
either branch.

## Link modes

`LINK_MODE` selects how the two embedded dependencies are linked and defaults to
`static`. It can be overridden per library, because distributions rarely ship
both in a usable form:

| Variable | Values | Default |
| --- | --- | --- |
| `LINK_MODE` | `static`, `shared` | `static` |
| `JANSSON_LINK_MODE` | `static`, `shared` | `$(LINK_MODE)` |
| `LIBJWT_LINK_MODE` | `static`, `shared` | `$(LINK_MODE)` |

In `static` mode the archive path is resolved from `pkg-config --variable=libdir`
and must be a position-independent archive; a non-PIC archive links into an
executable but not into a shared object, so it is rejected up front by a probe
with the real compiler. In `shared` mode the link line comes from
`pkg-config --libs`.

libcurl and OpenSSL are always linked as shared system libraries and have no
link-mode switch.

### Choosing a mode on an RPM-family distribution

- **Jansson: `shared` is supported.** Fedora and Enterprise Linux ship Jansson
  2.14, comfortably above the 2.7 floor, but `jansson-devel` contains no static
  archive. Build with `JANSSON_LINK_MODE=shared` and add a `jansson-devel`
  build dependency.
- **libjwt: `static` is currently the only usable mode.** Fedora and EPEL ship
  libjwt 1.12.1. That release predates the `jwt_checker_*` API entirely, so it
  is not a matter of raising or lowering the floor — 3.3.3 cannot be lowered to
  reach it without rewriting signature verification against a deprecated API.
  A package must therefore either co-package libjwt 3.3.3 or later and link it
  with `LIBJWT_LINK_MODE=shared`, or keep the default `static` mode against an
  archive built by `../scripts/ci/install-libjwt.sh`.

So the working configuration for a current Fedora or Enterprise Linux package
is:

```sh
make all JANSSON_LINK_MODE=shared LIBJWT_LINK_MODE=static
```

## Fatal warnings

Warnings are fatal by default (`WERROR=1`), and every gate in this repository
keeps that default. Packagers building a released tag on a newer toolchain can
set `WERROR=0`, which removes `-Werror` from both the module build and the
standalone test binaries. This exists so that a diagnostic added to a compiler
after a release cannot turn an unchanged, reviewed source tree into a build
failure. It does not disable any warning: `-Wall -Wextra -Wshadow` still apply
and the output should still be reviewed.

## Offline builds

`make all` and `make install` require no network access and run no dependency
installer. They invoke only the compiler, `pkg-config`, `install`, and
`scripts/ci/check-link-dependencies.sh`.

`scripts/ci/install-jansson.sh` and `scripts/ci/install-libjwt.sh` are separate
targets that this project's own CI uses to reproduce its reviewed archives; a
package replaces them with build dependencies, or with immutable RPM sources.
Both accept a pre-fetched archive through `JANSSON_SOURCE_ARCHIVE` and
`LIBJWT_SOURCE_ARCHIVE` and then perform no download, verifying the same
reviewed SHA-256 used for network retrieval.

This is enforced, not just asserted: every leg of the RPM platform
compatibility workflow runs `make all install DESTDIR=…` in a container started
with `--network none` and with no installer available.

## Architectures

See [`rpm-platforms.md`](rpm-platforms.md) for the architectures covered by a
build gate.
