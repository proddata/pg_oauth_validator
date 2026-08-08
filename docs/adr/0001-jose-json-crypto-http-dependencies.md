# ADR 0001: JOSE, JSON, cryptography, and HTTP dependencies

- Status: Accepted for Milestone 1
- Date: 2026-08-07
- Scope: Milestone 1 strict offline JWT validation

## Context

The validator processes attacker-controlled bearer tokens, JOSE headers,
claims, metadata, and JWKS responses inside PostgreSQL server processes. It
must not implement cryptographic primitives, inherit a library's permissive
defaults, or allow a convenience API to choose trust or network policy.

The dependency stack must support strict compact JWS processing, asymmetric
algorithm and key binding, JWK/JWKS parsing, duplicate JSON member rejection,
bounded inputs, deterministic failure, fuzzing, and clean C error boundaries.
It must remain usable with PostgreSQL 18 and 19 and be packageable without
embedding a private cryptographic implementation.

## Decision

Subject to the validation spike, use this stack:

1. **libjwt 3.3.3 or a later reviewed 3.3 patch release**, pinned by release
   tag and source digest, for JOSE/JWT, JWK/JWKS parsing, and signature
   verification.
2. **Jansson** as libjwt's JSON backend, with duplicate rejection required for
   every untrusted JSON document.
3. **OpenSSL 3.x from the operating-system PostgreSQL runtime**, restricted by
   local policy to the explicitly supported asymmetric algorithms and key
   parameters. Production packaging must require a vendor-supported OpenSSL
   release; it must not bundle a second private OpenSSL copy.
4. **libcurl from the operating system** for the later metadata/JWKS transport
   adapter. Do not enable or call libjwt's remote-JWKS convenience functions.

This selects implementations, not policy. The validator remains responsible
for token and response size limits, accepted serialization, type and algorithm
allowlists, critical-header handling, issuer/audience/scope rules, key
selection constraints, URL allowlisting, TLS verification, redirects,
timeouts, response limits, caching, refresh suppression, diagnostics, and
redaction.

Only compact, three-part JWS access tokens are in Milestone 1. libjwt features
for JWE, JSON JWS serialization, detached or unencoded payloads, embedded
token-provided keys, HMAC, remote URLs, and multiple signatures remain disabled
and must have rejection tests.

## Why this stack

libjwt is a C library with current JWK/JWKS and RFC 9068 support, an explicit
security policy, multiple crypto backends, algorithm/key-type binding, and a
recent security-hardening test suite. Version 3.3.3 is fixed to upstream commit
`602118d99d46ca5df71bda60d3df642135417f29` for the initial spike. Its commit
archive has SHA-256
`47e3d5d00fd60141dbacbcb9a0c2e1b277740364ac1d15b50728dccb7afdf23c`. Its
MPL-2.0 license permits dynamic linking while requiring modifications to the
library's covered source files to remain available.

Jansson exposes `JSON_REJECT_DUPLICATES`; without that flag, it retains the last
value. libjwt 3.3.3's Jansson adapter exposes the corresponding
`JWT_JSON_REJECT_DUPLICATES` capability, but the spike must prove that every
JWT and JWKS entry point actually enables it. If any public entry point does
not, adoption is blocked until strict pre-validation or an upstream fix is
reviewed and tested.

OpenSSL is already a normal PostgreSQL-platform dependency and provides the
required RSA and EC primitives. Using the system library keeps security updates
under the operating-system vendor's patch process and avoids two OpenSSL copies
with different lifecycle and provider state in one backend process.

libcurl is mature and gives the later transport adapter controls for protocol,
TLS, redirects, timeouts, and byte limits. Those controls are safe only when
configured together, so no library-owned fetch/cache helper may bypass the
validator's exact issuer-bound URL and outage policy.

## Packaging consequence

Debian 12/bookworm provides libjwt 1.10.2, not the selected API and hardening
baseline. The project must not silently compile against that older package.
Before implementation is merged, the build must perform a compile-time version
check and CI must consume a reproducibly pinned libjwt 3.3.x package or source
build. Vendoring a generated library snapshot in this repository is not the
default; downstream packages should express a versioned shared-library
dependency. The packaging method and source digest will be reviewed in the
implementation change.

## Rejected alternatives

### Direct OpenSSL plus a hand-written JOSE layer

Rejected because correct JWS signature representation, JWK conversion,
algorithm/key binding, and future rotation behavior would become bespoke
security-critical code. Small bounded parsing adapters are acceptable;
cryptographic and general JOSE implementations are not.

### cjose 0.6.x

cjose provides a compact C/OpenSSL/Jansson implementation and is packaged by
Debian, but its upstream activity, security-process visibility, and JWT/JWKS
policy surface are weaker for this project's requirements. Selecting it would
leave more claim, key-selection, and hardening behavior in local code.

### Latchset jose

Latchset jose is a capable Apache-2.0 C JOSE implementation, uses Jansson and
OpenSSL, rejects `none`, and is packaged by Debian. It is oriented toward the
general JOSE family, including encryption and broad serialization support. The
validator needs a narrower JWT/JWKS and RFC 9068 checking surface, for which
modern libjwt requires less local policy plumbing. Keep Latchset jose as the
fallback if the libjwt validation gates fail.

### C++ JWT libraries

Rejected for the current C17 module. They add an exception/ABI boundary inside
PostgreSQL without providing a compelling security or packaging advantage over
the selected C stack.

### libjwt remote JWKS fetching and cache

Rejected even though current libjwt includes response bounds and refresh
cooldowns. Its helper owns URL and cache decisions that must instead be bound to
the exact configured issuer, metadata result, PostgreSQL reload semantics, SSRF
policy, and documented outage behavior.

## Required validation spike

No production validator code may depend on this decision until CI proves all
of the following against the pinned version:

- PostgreSQL 18 and 19 builds link and unload cleanly under strict warnings.
- Library allocations and errors are contained without crossing PostgreSQL
  `ereport`/`longjmp` boundaries.
- Duplicate `alg`, `typ`, `kid`, `crit`, `iss`, `aud`, `exp`, and identity
  members are rejected, including duplicates nested in JWKS objects.
- Malformed, padded, non-canonical, oversized, and empty Base64URL segments are
  rejected before expensive cryptographic work.
- Compact JWS is accepted while JWE, JSON serialization, detached payloads,
  `b64=false`, compression, multiple signatures, and embedded/token-provided
  keys are rejected.
- `none`, all HMAC algorithms, algorithms outside the local allowlist, and
  algorithm/key-type mismatches are rejected.
- RSA key-size and EC-curve policy can be enforced before verification.
- A libjwt parse or verification failure cannot expose the bearer token,
  signature, complete claims, or key material.
- ASan, UBSan, leak checks, and a parser fuzz target run successfully.
- The dependency can be reproduced from an immutable source digest and its
  transitive runtime libraries are documented.

Failure of a gate reopens this ADR. It does not justify weakening the gate.

## Initial spike results

The standalone spike was run with ASan and UBSan against PostgreSQL 18 and
PostgreSQL 19 beta 2 build environments. A bounded libFuzzer target exercises
both the JWT checker and length-aware JWKS parser.

The spike confirms these useful library behaviors:

- invalid characters in a protected-header Base64URL segment are rejected;
- JWK key type and algorithm information is exposed for local policy;
- the library builds and links as C with Jansson and the system OpenSSL;
- libcurl, GnuTLS, MbedTLS, deprecated APIs, and library self-tests can be
  excluded from the production-shaped dependency build.

It also confirms that libjwt is intentionally broader than this validator:

- JWT header, payload, and JWKS parsing do not enable duplicate-member
  rejection; the last duplicate member is retained;
- padded Base64URL is accepted;
- parsing reaches the callback before an unexpected fourth compact segment is
  rejected during later signature processing;
- `none` verifies when no key is configured;
- valid HMAC tokens verify when an HMAC key is configured;
- `typ`, token-provided `jku`, and unsupported `crit` values are not rejected
  by default.

Therefore libjwt must not receive an untrusted token or JWKS directly from the
PostgreSQL callback or transport layer. The next implementation slice must add
a bounded strict-envelope/JSON pre-validator that rejects those forms before
libjwt is invoked. Signature verification will use a checker configured only
with an already selected asymmetric key and algorithm. Claim decisions will be
performed only after successful signature verification rather than through
libjwt's pre-signature claim checks.

The strict JWT-envelope pre-validator is now implemented as a standalone C17
component. It enforces compact-JWS shape, canonical unpadded Base64URL,
decoded-size limits, duplicate rejection throughout the protected header and
payload, strict access-token type and asymmetric-algorithm policy, mandatory
non-empty `kid`, and rejection of unsupported JOSE key, critical, compression,
and nesting headers. Its unit suite runs under ASan/UBSan and it has a bounded
libFuzzer target. Signature verification is wired through this boundary, and
the remaining JWK/JWKS gates described below are satisfied by the composed
validator and its negative test suite.

The strict JWKS boundary is also implemented independently of libjwt parsing.
It bounds document size and key count, rejects duplicate JSON members, selects
exactly one bounded `kid`, requires explicit signature intent, binds `alg` and
`kty` to local policy, rejects private key material, checks RSA modulus and
exponent constraints, and validates that ES256 coordinates form a non-infinite
point on P-256 with the system OpenSSL. The selected JSON remains untrusted
input owned by the parsed JWKS until the later libjwt signature adapter accepts
it.

The discovery-metadata boundary is likewise standalone. Jansson provides
bounded duplicate-rejecting JSON parsing, while libcurl's URL API provides
structural validation underneath stricter local rules: exact lowercase HTTPS
by default, no credentials or fragments, and an explicit development-only HTTP
opt-in. The parser preserves the returned `jwks_uri` exactly and does not fetch
it.

The first transport boundary now uses libcurl directly and requires an exact
administrator-approved host for every request. It disables implicit proxy use
and redirects, restricts protocols to HTTPS (or an explicit development HTTP
escape), verifies peer certificates and hostnames, bounds connection and total
time plus response bytes, and accepts only 2xx JSON/JWK-set media types.
DNS/IP policy beyond exact hostname authorization is not part of the Milestone
1 contract. Cache freshness and provider-outage behavior are defined by ADR
0002 and enforced by the shared-cache retrieval path.

The issuer-key retrieval stage composes these independent boundaries without
adding a new trust source. A configured metadata URL is fetched under its own
host policy, its issuer must exactly match administrator configuration, and its
`jwks_uri` is fetched only under a separately configured exact-host policy
before strict key selection. The stage performs no automatic retry and returns
typed, non-sensitive failure causes for the metadata transport, metadata
validation, JWKS transport, and JWKS validation phases.

The validator composes the complete Milestone 1 core in strict
trust order: hostile compact-JWS parsing, issuer-bound key retrieval, signature
verification, claims and scope validation, then stable identity construction.
Claims are never evaluated as trusted before signature success, and identity
memory is created only after every preceding stage succeeds. The PostgreSQL
callback invokes the orchestrator with defensive result initialization and
PostgreSQL-owned output allocation. Real PostgreSQL 18 and 19 tests prove
successful normal usermap authorization and rejection of an unmapped
administrative role.

The signature adapter now imports only that single selected public JWK, fixes
the checker to the already-approved RS256 or ES256 algorithm, and invokes no
libjwt callback, claim validation, URL, JWKS-selection, or caching facility.
It explicitly disables libjwt's built-in `exp` and `nbf` decisions; otherwise
the library evaluates those claims before signature verification against the
process wall clock. Time and all other claim policy run deterministically only
after a successful signature in the local claims stage.
Independent OpenSSL-generated RSA and P-256 tokens prove successful
verification and rejection of a different key, modified payload, and modified
signature under ASan/UBSan. Callback authorization occurs only after the same
complete validation sequence succeeds.

PostgreSQL exports generic JSON symbols that can interpose on Jansson symbols
inside a dynamically loaded extension. Real-backend testing demonstrated that
dynamic linking was unsafe even though standalone tests passed. The module
therefore embeds reviewed PIC static builds of libjwt and Jansson with local
symbol binding. OpenSSL and libcurl remain system shared libraries. CI tests
the resulting module inside real PostgreSQL backends on every supported major.

The dependency build also needs a scoped `-Wno-stringop-truncation` workaround
with GCC 12 because libjwt 3.3.3 enables `-Werror` and its error-copy macro
triggers that diagnostic. The project does not apply this suppression to its
own code. Recheck and remove it on every dependency upgrade.

## References

- [libjwt repository and supported standards](https://github.com/benmcollins/libjwt)
- [libjwt 3.3 releases and security-hardening notes](https://github.com/benmcollins/libjwt/releases)
- [Jansson decoding flags](https://jansson.readthedocs.io/en/latest/apiref.html#decoding)
- [OpenSSL release strategy](https://www.openssl-library.org/policies/releasestrat/)
- [libcurl security considerations](https://curl.se/libcurl/security.html)
- [Debian 12 libjwt package](https://packages.debian.org/bookworm/libjwt-dev)
- [Latchset jose](https://github.com/latchset/jose)
- [cjose](https://github.com/cisco/cjose)
