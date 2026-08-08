# Third-party notices

`pg_oauth_validator` uses the following third-party components in its token
validation path. The generated release archive includes the exact license
texts under `THIRD-PARTY-LICENSES/`.

## libjwt

The module statically incorporates libjwt 3.3.3 from commit
`602118d99d46ca5df71bda60d3df642135417f29`, licensed under the Mozilla Public
License 2.0.

The corresponding covered source is available from:

`https://github.com/benmcollins/libjwt/tree/602118d99d46ca5df71bda60d3df642135417f29`

The source archive used by the build is:

`https://codeload.github.com/benmcollins/libjwt/tar.gz/602118d99d46ca5df71bda60d3df642135417f29`

Its SHA-256 digest is
`47e3d5d00fd60141dbacbcb9a0c2e1b277740364ac1d15b50728dccb7afdf23c`.
The project's modifications, if any, are represented by the build scripts and
patches in the corresponding `pg_oauth_validator` source revision recorded in
`BUILD-MANIFEST`.

## Jansson

The module statically incorporates Jansson. Its exact version is recorded in
`BUILD-MANIFEST`. Jansson is distributed under the MIT license and contains
`src/dtoa.c`, whose additional notice is included in Debian's packaged
copyright file. The generated archive copies that complete packaged copyright
file as `THIRD-PARTY-LICENSES/JANSSON`.

## Dynamically linked libraries

The module dynamically links the deployment environment's libcurl, OpenSSL,
and system libraries. Their versions are recorded from the build environment
in `BUILD-MANIFEST`; operators must use supported, security-maintained runtime
packages.
