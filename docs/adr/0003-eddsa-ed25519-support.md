# ADR 0003: Opt-in EdDSA (Ed25519) access-token signatures

- Status: Proposed — requires review before release
- Date: 2026-10-01
- Scope: Strict offline JWT validation for PostgreSQL 18 and 19

## Context

The validator accepts `RS256` and `ES256` only. ADR 0001 and
`oauth-validator-plan.md` (decision 5) require that adding an algorithm go
through a reviewed policy with negative tests.

Some authorization servers sign access tokens with Ed25519 by default. A Go
OIDC provider built on go-jose publishes `alg: "EdDSA"` JWKs of type `OKP` /
`Ed25519`, and offers RS256 only as a per-client alternative. Requiring those
issuers to downgrade every PostgreSQL client to RSA is an avoidable
interoperability barrier, and Ed25519 has a simpler validation surface than
RSA: a fixed 32-byte public key, no modulus-size or exponent policy, and
deterministic signatures with no separate digest choice.

## Decision

Add `EdDSA` as a third, **opt-in** algorithm.

1. **Default unchanged.** `pg_oauth_validator.allowed_algorithms` still
   defaults to `RS256,ES256`. A deployment accepts EdDSA only when its
   administrator lists `EdDSA`. Upgrading changes no existing deployment's
   accepted token set.
2. **Ed25519 only.** The JWS header `alg` must be exactly `EdDSA`
   (case-sensitive, RFC 8037 §3.1), and the selected JWK must be
   `kty=OKP`, `crv=Ed25519`, a base64url `x` that decodes to exactly 32 bytes,
   and `alg=EdDSA`. Ed448, X25519, X448, other curve spellings, short or long
   keys, and keys carrying `d` are rejected before the key reaches libjwt.
   The curve is pinned by this project's code and is not inherited from
   libjwt.
3. **Same key-selection rules.** Existing rules are unchanged: exactly one
   `kid` match from the trusted JWKS, explicit signature intent (`use=sig` or
   `key_ops=[verify]`), the JWK `alg` must equal the token `alg` and be in the
   administrator allowlist, and no key-type/algorithm confusion. A token whose
   `alg` is `EdDSA` cannot select an RSA or EC key and vice versa.
4. **Mixed key sets stay valid.** Only the key matching the token's `kid` is
   inspected, so a JWKS that also publishes other keys is not rejected.
5. **Signature verification** uses libjwt's `JWT_ALG_EDDSA` checker bound to
   the single selected JWK, with libjwt's `exp`/`nbf` handling still disabled,
   exactly as for RS256/ES256.
6. **The fully-specified names `Ed25519` and `Ed448` (RFC 9864) are not
   accepted** by this decision. RFC 9864 (October 2025) deprecates the
   polymorphic `EdDSA` value in the JOSE registry, but `EdDSA` remains what
   deployed issuers emit today. Support for the fully-specified `Ed25519`
   name, and any sunset of `EdDSA`, needs a follow-up decision after the
   pinned libjwt's handling of that name is reviewed and tested.

## Consequences

- No bundled cryptography: verification uses the operating-system OpenSSL
  already in the dependency contract (ADR 0001). The OpenSSL FIPS-provider
  status of Ed25519 depends on the OpenSSL release; operators with a
  FIPS-restricted OpenSSL should keep `EdDSA` out of the allowlist.
- Operator-visible change: a new accepted value for `allowed_algorithms`.
  Rollback is removing `EdDSA` from the setting and reloading. Downgrading
  the module while `EdDSA` is still listed fails closed, because earlier
  modules reject an unknown allowlist entry as invalid configuration.
- Provider profiles are unaffected. This adds an algorithm, not provider
  detection.

## Required evidence

- Unit: accepted case; algorithm absent from allowlist; duplicate and
  case-variant allowlist entries; non-canonical header names (`eddsa`,
  `Ed25519`, trailing characters, embedded NUL); Ed448/X25519/short/long/
  private-key JWKs; wrong `kty`; missing, mismatched, or fully-specified JWK
  `alg`; `use=enc`; EdDSA token against an RSA key.
- Signature: a genuine Ed25519 signature verifies; a different trusted key,
  a tampered payload, and a tampered signature are rejected.
- Fuzz: JWKS and JWT-envelope targets exercise the EdDSA path with a valid
  seed.
- PostgreSQL integration: default denies EdDSA; an allowlist containing it
  admits a valid token; relabelled-`alg` and wrong-`kid` tokens are denied; an
  `EdDSA`-only allowlist denies RSA tokens; no token appears in logs or client
  errors.

## References

- [RFC 8037](https://www.rfc-editor.org/rfc/rfc8037): CFRG curves and
  signatures in JOSE (`EdDSA`, `OKP`, `Ed25519`).
- [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032): Ed25519.
- [RFC 9864](https://www.rfc-editor.org/rfc/rfc9864): fully-specified
  algorithms for JOSE and COSE.
