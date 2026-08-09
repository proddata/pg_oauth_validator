# Feature specification

## Purpose

This document defines the intended product behavior and security contract of the PostgreSQL OAuth validator. [`AGENTS.md`](AGENTS.md) defines engineering and contribution practices. [`oauth-validator-plan.md`](oauth-validator-plan.md) contains the broader design rationale, threat model, roadmap, and unresolved decisions.

The specification is deliberately narrower than the full roadmap. Features not listed as active are unsupported unless this document is updated through review.

## Product objective

Build a production-oriented PostgreSQL OAuth validator that accepts standards-compliant OAuth 2.0 access tokens for an explicitly configured PostgreSQL resource.

The validator keeps three decisions separate:

1. **Token validation:** Is this a genuine, currently valid access token issued by a trusted authorization server for this PostgreSQL resource?
2. **Authentication:** Which stable external principal does the token identify?
3. **Authorization:** May the validated principal connect to PostgreSQL under the configured access policy?

The validator supports PostgreSQL-owned identity matching and explicitly
delegated, claim-based role authorization. Both modes require complete token
validation and an authenticated identity.

## Implementation baseline

- The PostgreSQL module and validator callbacks are implemented in C17.
- PostgreSQL 18 is the stable production target.
- PostgreSQL 19 is a preview target during its beta period and will become a production target only after its stable release and final compatibility verification.
- Each PostgreSQL major version receives a separately compiled module and its own CI job because PostgreSQL does not provide cross-major ABI compatibility.
- The shared library uses PostgreSQL's native C ABI and PGXS build infrastructure.
- Protocol parsing and cryptographic primitives will come from reviewed, established libraries rather than bespoke implementations.
- The callback starts denied and authorizes only after the complete Milestone 1 validation slice succeeds.
- Issuer and required scopes come from the matched HBA rule through a small, isolated PostgreSQL-internal compatibility adapter.
- PostgreSQL 19 registers `validator.policy` through the supported custom HBA option API so a rule can select additional named validation policy.
- A validated immutable policy combines matched-HBA issuer/scopes with configured audience, algorithms, token type, identity claim, clock skew, and token-size limit.
- Audience is mandatory and has no implicit default. Algorithms default to `RS256,ES256`, token type to `at+jwt`, identity claim to `sub`, clock skew to 60 seconds, and maximum token size to 16 KiB.

## Active scope: Milestone 1

Milestone 1 provides strict offline validation of signed JWT access tokens:

- PostgreSQL 18 and 19 OAuth validator module initialization and callback integration, with version-specific result handling.
- Explicit trusted issuer configuration and exact issuer validation.
- Standards-based discovery or explicitly configured metadata/JWKS location.
- Strict access-token type enforcement through the selected profile.
- Local asymmetric signing-algorithm allowlist.
- Hardened JWKS key selection and signature verification.
- Mandatory configured audience validation.
- Required expiry and stable authenticated identity.
- Bounded clock skew and resource limits.
- Required connection-scope validation using the strict `scope` claim and all
  scopes configured by the matched HBA rule.
- Configurable authenticated-identity claim, defaulting to `sub`.
- Direct or issuer-qualified authenticated-identity representation.
- Normal PostgreSQL exact-name/usermap authorization and opt-in delegated exact
  role-claim authorization.
- Controlled single JWKS refresh for legitimate signing-key rotation.
- Negative unit and integration test coverage for the threat model.
- Sanitized, stable diagnostic codes without token or secret disclosure.

## Required validation pipeline

All stages fail closed and execute in this trust order:

1. Select an administrator-configured trusted issuer/profile; never select trust from token claims.
2. Retrieve bounded metadata/JWKS only from configured or standards-derived trusted locations.
3. Parse the token under strict size and complexity limits.
4. Enforce the accepted access-token type.
5. Select a trusted signing key and locally allowed asymmetric algorithm.
6. Verify the signature before using claims for decisions.
7. Validate issuer, audience, expiry, activation time, identity, and other configured claims.
8. Validate required PostgreSQL connection scopes/entitlements.
9. Construct a safe, stable authenticated identity.
10. Return resource-level authorization and the identity to PostgreSQL for usermap processing.

Callback outputs must begin denied. Success at an earlier stage never compensates for failure or uncertainty at a later stage.

## Security invariants

- Only OAuth access tokens are accepted. ID tokens are rejected even when signed by the trusted issuer and key.
- `iss`, `aud`, `exp`, and the configured stable identity claim are mandatory and correctly typed.
- Audience matching is exact and requires at least one configured PostgreSQL resource identifier.
- Issuer matching is exact; no substring detection or silent trailing-slash normalization is allowed.
- Direct identity returns the configured string claim unchanged. Issuer-qualified
  identity returns `v1.<base64url(issuer)>.<base64url(configured-claim)>`.
- Delegated authorization requires the exact requested PostgreSQL role in a
  bounded configured string-array claim.
- `openid` alone is not sufficient authorization to connect to PostgreSQL.
- Signing algorithms come from a local allowlist. `none`, algorithm confusion, and remote HMAC keys are rejected by default.
- Key selection uses trusted JWKS data. Missing or ambiguous key identifiers are rejected.
- Token-provided `jku` and `x5u` locations are never followed.
- Inputs, network activity, retries, refreshes, caches, parsing, and clock skew are bounded.
- Complete bearer tokens, signatures, secrets, and sensitive claim sets are never logged or returned to clients.
- Detailed diagnostics remain in protected server logs; client-facing errors are generic.
- Network or internal failures cannot produce authorization success.

## Identity and role mapping

In normal identity mode:

```text
authorized = valid token for PostgreSQL AND required connection privileges
authn_id   = stable external identity
```

Without `map=`, PostgreSQL requires `authn_id` to exactly equal the requested
role. With `map=`, PostgreSQL applies the selected `pg_ident.conf` usermap.

In delegated roles mode:

```text
authorized = valid token for PostgreSQL AND required connection privileges
             AND requested role is an exact member of the configured roles claim
authn_id   = configured external identity for auditing
```

Delegated mode requires `delegate_ident_mapping=1` in the matched HBA rule and
an explicitly selected validator authorization mode. The HBA `USER` field
should independently allowlist reachable roles. It must not be combined with
`map=`.

The validator applies no special denylist, allowlist, or privilege
classification to delegated role names. A privileged role, including a
superuser, may be authorized when the HBA `USER` field permits that requested
role and the validated token contains its exact name. This is deliberate: the
HBA rule and issuer-side claim policy are the administrator-controlled
authorization boundaries. Deployments should enumerate delegated roles rather
than use `all` or a broad `+group` unless that wider authority is intentional.

## Metadata, JWKS, and network behavior

- Discovery parsing is independent of network transport and accepts only a bounded JSON object with a unique, exact configured `issuer` and one structurally valid absolute `jwks_uri`.
- The transport adapter requires an exact allowed host, disables environment proxies and redirects, verifies TLS, bounds connection/total time and response bytes, and accepts only successful JSON responses.
- Issuer-bound retrieval composes configured metadata fetch, exact issuer validation, separately allowlisted JWKS fetch, and strict single-key selection without implicit retry or cache behavior.
- The standalone orchestrator executes envelope, key retrieval, signature, claims/scopes, and identity stages in fail-closed order while preserving typed non-sensitive failures.
- Production metadata and JWKS retrieval requires HTTPS with certificate verification.
- Any development-only HTTP option must be explicit, disabled by default, and clearly unsafe for production.
- Metadata `issuer` must exactly equal the configured issuer.
- URL schemes, redirects, response sizes, timeouts, and allowed hosts are restricted by policy.
- Cache entries are separated by exact URL, issuer, and provider profile.
- Unknown signing keys may trigger at most one controlled refresh, subject to per-issuer rate limiting.
- Failed refreshes do not erase a still-permitted last-known-good key set.
- Any stale-key availability window is bounded and administrator-defined.
- Provider outage behavior is deterministic and documented; an unverifiable token is never accepted for availability.
- PostgreSQL locks are not held during network operations.

## Provider profiles

The validation core implements strict standards behavior. Any provider compatibility behavior must use a named, explicitly selected profile bound to an exact trusted issuer.

A supported profile documents and tests:

- acceptable issuer rules;
- metadata and JWKS behavior;
- token type and signing algorithms;
- audience semantics;
- scope claim syntax;
- stable identity claim;
- optional group/role claim semantics;
- provider versions and known limitations.

Implicit provider detection from issuer-hostname substrings is unsupported.

## Explicitly deferred or unsupported

The following are outside Milestone 1:

- browser authorization and device-code flows performed by clients;
- acting as an authorization server or identity provider;
- automatic PostgreSQL role provisioning or deletion;
- accepting arbitrary JWTs solely because their signature verifies;
- accepting opaque tokens or using token introspection;
- revocation-aware online validation;
- implicit provider compatibility;
- implicit provider-group transformations into PostgreSQL roles;
- anonymous or identity-less access;
- automatic acceptance of arbitrary provider roles without explicit delegated
  configuration and an HBA role boundary.

## Acceptance criteria for initial security review

The implementation is ready for an initial security review only when:

- a correctly signed token for another audience is rejected;
- a trusted issuer's ID token is rejected as an access token;
- tokens missing `iss`, `exp`, `aud`, or the configured identity are rejected;
- wrong-type and malformed security-sensitive claims are rejected;
- only administrator-allowed asymmetric algorithms and valid keys are accepted;
- rotated signing keys can be adopted without attacker-driven refresh storms;
- a valid principal cannot assume a role that fails normal exact-name/usermap
  authorization or delegated exact role-claim authorization;
- delegated mapping fails unless both HBA delegation and validator delegated
  policy are explicitly enabled;
- malformed, duplicate, oversized, missing, and insufficient delegated role
  claims are rejected;
- no token or secret appears in normal, debug, or error logs;
- network failures and cache behavior are deterministic and documented;
- negative tests cover the documented threat model and run in CI;
- PostgreSQL integration tests exercise real connections over TLS.

## Decisions required before their related implementation

These remain product/security decisions, not implementation details:

1. Canonical PostgreSQL audience identifier and how each provider/client requests it.
2. Whether the first release is strict RFC 9068 only or includes named compatibility profiles.
3. Metadata/JWKS freshness bounds and provider-outage policy are resolved by
   [`ADR 0002`](docs/adr/0002-metadata-jwks-cache-and-outage-policy.md); the
   implementation gates in that decision remain active work.
4. Issuer and scopes are HBA-owned; audience is currently a cluster-wide GUC.
   Per-rule audience selection is deferred to named policies.
5. Reload uses complete canonical-key separation. Shared-memory layout changes
   and same-path CA bundle replacement require restart or an explicitly
   versioned CA path.

Resolve and document each decision before making it part of the public configuration or compatibility contract.
