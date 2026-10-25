# ADR 0001: JOSE, JSON, cryptography, and HTTP dependencies

- Status: Accepted for Milestone 1
- Date: 2026-08-07
- Scope: Strict offline JWT validation

## Decision

Use reviewed, pinned libjwt 3.3.3 (or a later reviewed 3.3 patch) with Jansson
for JOSE/signature verification, system OpenSSL 3.x for cryptography, and system
libcurl for metadata/JWKS HTTP. Production OpenSSL/libcurl must be vendor
supported. Source commits and digests belong in the dependency installers.

Embed PIC static libjwt and Jansson with local symbol binding. Real-backend
testing found PostgreSQL JSON symbols interposing on dynamically linked Jansson;
standalone success did not establish safe module loading. Keep OpenSSL/libcurl
dynamically linked to system libraries rather than bundling private copies.
This supersedes the initial preference for a shared libjwt package.

The library provides mechanisms, not trust policy. Project-owned boundaries
enforce bounded compact three-part JWS, canonical unpadded Base64URL, duplicate
JSON rejection, access-token type, asymmetric algorithm/key binding, critical
headers, claims, URL/TLS policy, refresh/cache limits and safe diagnostics.
Do not use libjwt remote-JWKS, cache, callback or pre-signature claim facilities.
JWE, JSON JWS, detached/unencoded payloads, compression, embedded keys, HMAC and
multiple signatures remain outside the accepted format and need rejection tests.

## Boundary evidence and consequences

The initial ASan/UBSan spike in PostgreSQL 18/19 beta 2 environments found that
libjwt accepts padded Base64URL, duplicate JSON members (last wins), `none`
without a key and HMAC with an HMAC key. It does not enforce `typ`, `jku` or
unsupported `crit` by default, and an extra compact segment can reach its callback
before rejection. These behaviors require strict pre-validation, not reliance
on library defaults. Jansson duplicate rejection must be explicitly enabled.

The independent envelope and JWKS parsers therefore run before libjwt. The JWKS
boundary bounds size/count/identifiers, selects exactly one key, checks signing
intent and algorithm/type, rejects private material and enforces RSA/curve
constraints. The signature adapter imports only the selected public JWK and
fixes the approved algorithm. It disables libjwt `exp`/`nbf` decisions because
those run before signature verification against the process wall clock. Local
claim/time policy runs only after a successful signature.

Discovery and transport stay separate: validated metadata supplies only an exact
issuer-bound URL; project HTTP code enforces host, HTTPS, certificates, no
redirect/proxy, response/time bounds and typed redacted failures. Exact hostname
authorization does not claim additional DNS/IP filtering. Cache policy is
[ADR 0002](0002-metadata-jwks-cache-and-outage-policy.md).

On dependency changes, repeat the spike, malformed/duplicate/oversized input,
format/algorithm confusion, key and signature negative tests, diagnostics,
sanitizers/leaks and fuzz checks. Confirm PostgreSQL-major real-backend loading,
allocation/error containment across PostgreSQL `longjmp` boundaries, reproducible
pins and runtime dependency inventory. Failed gates reopen the decision; they
do not justify weaker policy. Commands are in [development](../development.md).

The initial dependency-only GCC 12 `-Wno-stringop-truncation` workaround addresses
libjwt's error-copy macro under its own `-Werror`; it must never apply to project
code and must be reconsidered at each dependency upgrade.

## Alternatives rejected

- Hand-written JOSE over OpenSSL: too much bespoke signature/key conversion and
  algorithm-binding code. Small bounded parsing adapters remain appropriate.
- cjose 0.6.x: weaker maintenance/security-process visibility and policy surface.
- Latchset jose: broader general JOSE surface and more local JWT policy plumbing;
  retain as a fallback if libjwt fails the gates.
- C++ JWT libraries: unnecessary exception/ABI boundary in the C17 module.
- Library-owned fetching/caching: cannot own exact issuer, SSRF, reload,
  resource-bound and outage policy.

## References

- [libjwt repository and supported standards](https://github.com/benmcollins/libjwt)
- [libjwt 3.3 releases and security-hardening notes](https://github.com/benmcollins/libjwt/releases)
- [Jansson decoding flags](https://jansson.readthedocs.io/en/latest/apiref.html#decoding)
- [OpenSSL release strategy](https://www.openssl-library.org/policies/releasestrat/)
- [libcurl security considerations](https://curl.se/libcurl/security.html)
- [Debian 12 libjwt package](https://packages.debian.org/bookworm/libjwt-dev)
- [Latchset jose](https://github.com/latchset/jose)
- [cjose](https://github.com/cisco/cjose)
