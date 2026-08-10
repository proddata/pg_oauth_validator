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

The module statically incorporates Jansson 2.15.1 from commit
`dbb5fb3636e155fccfce4cd215de752779bd6971`, licensed under the MIT license.
Its exact version is also recorded in `BUILD-MANIFEST`.

The corresponding source is available from:

`https://github.com/akheron/jansson/tree/dbb5fb3636e155fccfce4cd215de752779bd6971`

The source archive used by the build is:

`https://codeload.github.com/akheron/jansson/tar.gz/dbb5fb3636e155fccfce4cd215de752779bd6971`

Its SHA-256 digest is
`65084e4e43de9840d66a0604c8d9d9c499b2fc0db52c05730e3b6ac3c11ed66f`.
Jansson contains `src/dtoa.c`, whose additional notice is part of the upstream
`LICENSE` file. The generated archive copies that complete file as
`THIRD-PARTY-LICENSES/JANSSON`.

## Dynamically linked libraries

The module dynamically links the deployment environment's libcurl, OpenSSL,
and system libraries. Their versions are recorded from the build environment
in `BUILD-MANIFEST`; operators must use supported, security-maintained runtime
packages.

## Playground-only node-postgres snapshot

The optional `playground` Node interoperability-client image downloads and
builds the pure-JavaScript client from node-postgres pull request 3694 at commit
`68d58096e99296776a08d9c4333f5d742a8d2675`, licensed under the MIT license.
It is not linked into or distributed with the validator module. The image build
pins the source archive SHA-256 and installs the dependency graph locked by that
source revision.
