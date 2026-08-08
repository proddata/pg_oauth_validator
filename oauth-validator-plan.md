# PostgreSQL OAuth validator: design and implementation plan

Status: initial planning draft
Targets: PostgreSQL 18 stable and PostgreSQL 19 preview OAuth validator modules
Primary use case: validate OAuth 2.0 access tokens and authorize PostgreSQL connections without database passwords

## 1. Purpose

Build a production-oriented PostgreSQL OAuth validator that has an explicit security model, interoperates with standards-compliant authorization servers, and supports provider-specific behavior only through narrow, documented profiles.

The validator must keep three decisions separate:

1. **Token validation:** Is this a genuine, currently valid access token issued by a trusted authorization server for this PostgreSQL resource?
2. **Authentication:** Which stable external principal does the token identify?
3. **Authorization:** May that principal connect as the PostgreSQL role requested by the client?

The default authorization mode should return an authenticated identity to PostgreSQL and let `pg_ident.conf` perform identity-to-role mapping. Validator-managed role authorization (`delegate_ident_mapping`) should be a separate, opt-in mode with stricter configuration requirements.

## 2. Non-goals

The first production release should not:

- implement the browser-facing authorization or device-code flow performed by libpq;
- act as an OAuth authorization server or identity provider;
- provision or delete PostgreSQL roles automatically;
- accept arbitrary JWT formats based only on successful signature verification;
- infer PostgreSQL role membership from unconfigured token claims;
- make an access token valid for PostgreSQL merely because it contains `openid`;
- support anonymous access or a missing authenticated identity;
- support opaque tokens unless token introspection is explicitly implemented.

## 3. Threat model

The design must account for:

- a token issued by a trusted issuer but intended for a different API;
- substitution of an ID token or another JWT type for an access token;
- forged, unsigned, weakly signed, or algorithm-confused tokens;
- stale signing keys, key rotation, and attacker-selected key identifiers;
- expired and not-yet-valid tokens, including bounded clock skew;
- reuse of a stolen bearer token until expiry;
- a valid user attempting to assume a different PostgreSQL role;
- ambiguous or mutable identity claims such as email addresses;
- malicious or compromised discovery and JWKS endpoints;
- SSRF, redirects, oversized responses, slow endpoints, and cache poisoning;
- authorization bypass caused by empty or incorrectly interpreted scopes;
- provider-specific claim formats being applied to the wrong issuer;
- secrets or complete bearer tokens appearing in logs;
- denial of service through repeated network fetches or expensive JWT processing.

Bearer-token theft cannot be fully mitigated by offline JWT validation. Production deployments must use TLS for PostgreSQL connections, keep access tokens short-lived, and decide whether immediate revocation requires token introspection.

## 4. Standards and contracts

The implementation should document which profile it supports and follow, at minimum:

- PostgreSQL 18 and 19 OAuth validator initialization, callback, memory, and error-handling contracts;
- OAuth 2.0 bearer-token usage;
- OAuth 2.0 Authorization Server Metadata and/or OpenID Connect Discovery;
- JWT, JWS, and JWK processing rules;
- JWT Best Current Practices (RFC 8725);
- the JWT Profile for OAuth 2.0 Access Tokens (RFC 9068) when the provider supports it;
- OAuth 2.0 Resource Indicators (RFC 8707) where used to request a PostgreSQL audience;
- OAuth 2.0 Token Introspection (RFC 7662) if opaque-token or revocation-aware validation is added.

OIDC discovery may be used to locate metadata and keys, but the object being accepted by PostgreSQL is an OAuth **access token**, not an OIDC ID token.

## 5. PostgreSQL integration

### 5.1 Module lifecycle

The module must:

- expose `_PG_oauth_validator_module_init` with the correct PostgreSQL magic and callbacks;
- define configuration through PostgreSQL GUCs with appropriate contexts;
- initialize every callback result to deny;
- use PostgreSQL memory allocation for values returned to the server;
- contain C++ exceptions and PostgreSQL `longjmp` behavior at safe boundaries;
- avoid leaking state between backend processes or validation attempts;
- support clean shared-cache startup, attachment, and shutdown;
- return callback status separately from the authorization result, as required by PostgreSQL.

### 5.1.1 Matched HBA policy inputs

Read the built-in OAuth issuer and required scope string from the matched HBA rule. PostgreSQL 18's public validator callback does not expose those fields, so keep access through `MyProcPort->hba` inside a small version-specific compatibility adapter and test it against every supported PostgreSQL major version.

For PostgreSQL 19 and later, register the custom `validator.policy` HBA option through `RegisterOAuthHBAOptions()` and retrieve it with `GetOAuthHBAOption()`. The option selects additional named policy such as audience and provider profile; it does not replace the built-in HBA issuer or scope.

Fail closed if the backend port, matched HBA rule, issuer, or scope is unavailable. Never modify or retain PostgreSQL-owned HBA strings beyond the callback lifetime.

### 5.2 Normal usermap mode — required

In the default mode:

1. Validate the token completely.
2. Extract a stable authenticated identifier.
3. Set `authn_id` to that identifier.
4. Set `authorized` only after resource-level token authorization succeeds.
5. Let PostgreSQL apply `map=`/`pg_ident.conf` to determine whether the identity may assume the requested role.

The identity representation is a versioned, issuer-qualified pair. Both UTF-8
components are encoded independently as canonical unpadded Base64URL:

```text
v1.<base64url(issuer)>.<base64url(stable-claim)>
```

For example, issuer `https://idp.example/realms/acme` and subject
`248289761001` produce:

```text
v1.aHR0cHM6Ly9pZHAuZXhhbXBsZS9yZWFsbXMvYWNtZQ.MjQ4Mjg5NzYxMDAx
```

The final value is limited to 1024 ASCII bytes. This avoids issuer collisions
and delimiter ambiguity while remaining reversible and safe to place literally
in `pg_ident.conf`. A provider profile may select another immutable claim, but
it does not change this outer encoding. Mutable usernames and email addresses
must not be defaults.

### 5.3 Delegated identity mapping — optional, later milestone

When `delegate_ident_mapping` is enabled, PostgreSQL deliberately does not compare `authn_id` with `pg_ident.conf`. The validator must therefore use the callback's requested `role` argument and prove that the token holder may assume that exact role.

Delegation must require all of the following:

- an explicit validator setting enabling delegated authorization;
- an explicit claim-to-role policy;
- exact role matching or a configured transformation/mapping;
- rejection of missing, malformed, or ambiguous role claims;
- a denylist or separate policy for administrative roles such as superusers;
- issuer-specific namespaces for groups, roles, and entitlements;
- audit logging of subject, requested role, policy used, and decision;
- tests proving that a user authorized for one role cannot request another.

Possible policies include:

- token claim contains exact PostgreSQL role names;
- provider group to PostgreSQL role mappings configured locally;
- entitlement strings mapped locally to roles;
- an external policy decision point.

Raw provider groups must never be treated as PostgreSQL roles without an explicit policy. Until this feature exists, documentation must say that `delegate_ident_mapping` is unsupported and unsafe with the validator.

## 6. Validation pipeline

All checks must fail closed. The recommended order is:

### 6.1 Select trusted issuer configuration

- Obtain the expected issuer from the matched HBA rule or an administrator-defined issuer profile.
- Require an exact issuer match; document trailing-slash normalization and avoid silent normalization.
- Do not choose an issuer from an unverified token claim.
- Bind every provider compatibility rule to an exact configured issuer/profile.

### 6.2 Retrieve and validate metadata

- Fetch a configured metadata URL or derive the standard discovery URL.
- Require HTTPS in production, with an explicit development-only HTTP escape hatch.
- Verify that metadata `issuer` exactly matches the configured issuer.
- Require a valid `jwks_uri` for local JWT validation.
- Apply response size limits, timeouts, TLS verification, redirect restrictions, and allowed URL schemes.
- Do not follow token-provided `jku` or `x5u` URLs.
- Optionally restrict discovery and JWKS hosts according to administrator policy.

### 6.3 Parse token under resource limits

- Enforce a maximum token size.
- Require a three-part signed JWT for local validation.
- Reject malformed Base64URL, JSON, duplicate security-sensitive claims, unsupported nesting, and unsupported compression.
- Never log the complete token or signature.

### 6.4 Enforce token type

For an RFC 9068 profile:

- require `typ` to be `at+jwt` or `application/at+jwt`;
- reject ID-token types and missing or unexpected types.

Providers that do not emit `at+jwt` need a named compatibility profile with explicit alternative rules. A generic "ignore type" default is not acceptable for production.

### 6.5 Select key and algorithm safely

- Match `kid` only against keys from the trusted issuer's JWKS.
- Reject a missing `kid` when key selection would be ambiguous.
- Require keys to be intended for signatures (`use=sig`) or have compatible `key_ops`.
- Maintain a local allowlist of accepted algorithms; do not derive the entire policy from the token or JWKS.
- Bind each key to exactly one permitted algorithm.
- Reject `none` and unsupported algorithms.
- Prefer asymmetric algorithms such as RS256 or ES256.
- Disable symmetric HMAC keys by default for remote discovery/JWKS deployments.
- Validate RSA key size, EC curve, and all cryptographic key inputs.

### 6.6 Verify signature

- Verify the signature before using untrusted claims for an authorization decision.
- On an unknown `kid` or signature failure potentially caused by rotation, perform at most one controlled JWKS refresh and retry.
- Rate-limit refreshes so attacker-selected `kid` values cannot cause an outbound request storm.

### 6.7 Validate required claims

Require and validate:

- `iss`: exact trusted issuer;
- `aud`: contains the configured PostgreSQL resource identifier;
- `exp`: present and later than current time;
- `sub` or the configured stable identity claim: present and correctly typed.

Also validate when present or required by profile:

- `nbf`: token is active;
- `iat`: plausible and not unreasonably far in the future;
- `jti`: correctly typed and available for audit/revocation integrations;
- `client_id`/`azp`: only if client restrictions are part of local policy.

Clock-skew leeway must be small, bounded, and configurable.

### 6.8 Validate audience — mandatory

Audience validation is the primary defense against accepting a token issued for another resource server.

- Configure one or more exact PostgreSQL resource identifiers.
- Support both the JWT string and array forms of `aud`.
- Require at least one configured audience to match according to an explicitly documented policy.
- Do not use the OAuth client ID as the resource audience unless that is the provider's documented model.
- Reject missing or empty audiences.

Example:

```conf
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
```

The client/provider configuration should request that resource through RFC 8707 `resource`, a provider-specific `audience` parameter, or a provider-side default resource mapping.

### 6.9 Validate scopes and entitlements

Scopes remain a useful authorization control, but they supplement rather than replace audience validation.

- Read provider-configured access-token scope claims, normally `scope` and, where required, `scp`.
- Support documented string and array representations without coercing unrelated types.
- Require all configured connection scopes by default.
- Recommend a dedicated scope such as `connect:postgres`; `openid` alone is insufficient.
- Reject a missing scope when scopes are required.
- Treat an empty required-scope configuration as an explicit security-sensitive option, not an accidental bypass.
- Bind provider-specific scope rewriting to an exact provider profile.

Scopes should normally answer "may this token access PostgreSQL?" Role claims or local usermaps should answer "which PostgreSQL role may this principal use?"

### 6.10 Determine identity

- Extract only a configured, correctly typed claim.
- Prefer immutable opaque subject identifiers.
- Qualify the identity with issuer when multiple issuers are possible.
- Define maximum length and encoding rules compatible with PostgreSQL usermaps and logs.
- Reject control characters, embedded NULs, or invalid encodings.
- Document exactly what `authn_id` contains for DBAs writing `pg_ident.conf`.

### 6.11 Make authorization decision

In normal mode:

```text
authorized = valid token for PostgreSQL AND required connection privileges
authn_id   = stable external identity
```

PostgreSQL then performs the identity-to-role mapping.

In delegated mode:

```text
authorized = normal-mode checks AND token/policy explicitly permits requested role
authn_id   = stable external identity for auditing
```

The code must not infer delegated mode solely because an identity mapping fails.

## 7. Configuration model

Configuration should be explicit and safe by default. Candidate settings:

```conf
pg_oauth_validator.profile = 'rfc9068'
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
pg_oauth_validator.allowed_algorithms = 'RS256 ES256'
pg_oauth_validator.authn_claim = 'sub'
pg_oauth_validator.qualify_subject_with_issuer = on
pg_oauth_validator.required_token_type = 'at+jwt'
pg_oauth_validator.clock_skew = '60s'
pg_oauth_validator.http_timeout = '5s'
pg_oauth_validator.max_token_size = '16kB'
pg_oauth_validator.max_metadata_size = '64kB'
pg_oauth_validator.allowed_jwks_hosts = ''
pg_oauth_validator.ca_file = ''
pg_oauth_validator.metadata_default_ttl = '5min'
pg_oauth_validator.jwks_default_ttl = '5min'
pg_oauth_validator.cache_max_ttl = '1h'
pg_oauth_validator.jwks_stale_grace = '0s'
pg_oauth_validator.unknown_kid_refresh_cooldown = '30s'
pg_oauth_validator.cache_max_entries = 32
pg_oauth_validator.allow_insecure_http = off
pg_oauth_validator.delegated_authorization = off
```

Initial implementation decision: issuer and required scopes come from the matched HBA rule. `audiences` is mandatory and has no default. `allowed_algorithms`, `required_token_type`, `authn_claim`, `clock_skew`, and `max_token_size` use the restrictive defaults shown above. PostgreSQL 19's optional `validator.policy` currently resolves only to `default`; unknown names fail closed until a reviewed named-policy store exists.

Initial JWKS host decision: permit the exact trusted discovery-endpoint host
automatically. Cross-host `jwks_uri` values fail closed unless their hostname is
listed explicitly in `allowed_jwks_hosts`. The list is empty by default, uses
exact hostname matching without wildcards or suffix matching, and never permits
redirects to bypass the same restriction.

Metadata/JWKS freshness, outage, rotation and reload behavior is decided in
[`docs/adr/0002-metadata-jwks-cache-and-outage-policy.md`](docs/adr/0002-metadata-jwks-cache-and-outage-policy.md).
The fail-closed default permits only fresh entries; stale JWKS use requires an
explicit bounded operator setting. Cache entries are fixed-size, shared across
backends, separated by every trust-relevant input, and invalidated by a
versioned canonical effective-policy key. No PostgreSQL lock is held during
network operations.

Configuration decisions:

- Required audiences are currently a cluster-wide `PGC_SIGHUP` GUC. Per-rule
  audiences require the future named-policy store and are not inferred.
- PostgreSQL 19 HBA rules may select only the implemented `default` policy;
  PostgreSQL 18 uses that policy implicitly.
- Current validator settings are reloadable. Canonical-key separation prevents
  changed effective policy from reusing old entries. Shared-memory layout and
  capacity changes require restart. A CA bundle replaced at the same path also
  requires a versioned path plus reload or a restart.
- Initial decision: all production identities use the versioned,
  issuer-qualified Base64URL representation defined in section 5.2.
- Initial decision: prohibit an empty required-scope set. Supporting an
  explicitly scopeless policy requires a named reviewed profile and must not
  be inferred from missing HBA configuration.

Secrets, including introspection credentials, must not be placed in `pg_hba.conf` or emitted in `pg_settings` to unauthorized users.

## 8. Provider profiles

The core validator should implement strict standards behavior. Compatibility differences should be isolated in named profiles, for example:

- `rfc9068` — strict `at+jwt`, issuer, audience, lifetime, and asymmetric-signature checks;
- `keycloak` — documented Keycloak claim and resource/audience behavior;
- `auth0` — documented API audience and access-token type behavior;
- `entra-v2` — tenant-bound issuer, version, scope, and identity rules.

Every profile must state:

- exact acceptable issuer patterns;
- metadata and JWKS behavior;
- required token type and signing algorithms;
- audience semantics;
- scope claim syntax;
- stable identity claim;
- optional groups/roles claim semantics;
- known limitations and tested provider versions.

Provider detection based only on a substring in an issuer hostname should be replaced by explicit administrator selection plus issuer validation.

## 9. Metadata and JWKS cache

The cache must support both correctness and availability:

- separate metadata and JWKS entries by exact URL and issuer profile;
- honor bounded HTTP freshness information;
- impose administrator-defined minimum and maximum TTLs;
- retain a last-known-good key set for a bounded stale period during provider outages;
- refresh once on unknown `kid`, with per-issuer rate limiting;
- prevent unbounded entry count and response sizes;
- avoid holding PostgreSQL locks during network operations;
- prevent a failed refresh from erasing usable keys prematurely;
- expose cache hit, miss, refresh, stale-use, and error metrics/log events;
- invalidate affected entries when issuer security configuration changes.

The behavior during provider outage must be explicit: either fail closed immediately or allow bounded use of previously validated metadata and keys. It must never accept an unverifiable token merely to preserve availability.

## 10. Online introspection mode

An optional later mode may validate opaque tokens or provide stronger revocation behavior through RFC 7662 introspection.

It would require:

- authenticated TLS requests to a configured introspection endpoint;
- secure storage and rotation of client credentials;
- validation of `active`, issuer/resource binding, audience, expiry, scopes, and identity;
- tightly bounded positive caching that never exceeds token expiry;
- cautious negative caching;
- explicit fail-closed behavior during endpoint failure;
- no token value in logs or cache keys exposed to SQL users.

Local JWT validation and introspection should be explicit modes, not an ambiguous fallback chain.

## 11. Logging, audit, and diagnostics

The validator should provide actionable diagnostics without leaking credentials.

Log or expose at suitable levels:

- a stable decision/error code;
- trusted issuer/profile;
- matched audience policy;
- authenticated identity after sanitization;
- requested PostgreSQL role;
- normal versus delegated mapping mode;
- scope mismatch details where safe;
- key ID and cache behavior;
- metadata/JWKS/introspection latency and failure category.

Never log:

- the bearer token;
- JWT signatures;
- symmetric keys or introspection secrets;
- complete sensitive claim sets.

Errors returned to clients should be generic. More detailed reasons belong in protected PostgreSQL logs.

## 12. Testing strategy

### 12.1 Unit tests

Cover at least:

- issuer, audience string/array, expiry, `nbf`, and clock-skew boundaries;
- missing and incorrectly typed required claims;
- allowed and disallowed algorithms;
- RSA/EC key validation and key/algorithm mismatches;
- missing, duplicate, unknown, and rotated `kid` values;
- `typ` enforcement and ID-token substitution attempts;
- scope string/array parsing and exact matching;
- issuer-qualified identity construction and invalid characters;
- normal and delegated authorization decisions;
- provider-profile isolation;
- cache expiration, refresh suppression, and stale-key policy.

### 12.2 Negative security fixtures

Maintain tokens for:

- valid signature but wrong audience;
- valid issuer token for another API;
- ID token signed by the same issuer/key;
- expired and not-yet-valid tokens;
- missing `exp`, `aud`, `sub`, `kid`, or `typ`;
- `alg=none` and algorithm-confusion attempts;
- attacker-selected `jku`/`x5u` headers;
- valid scope but unauthorized PostgreSQL role;
- forged group/role claim;
- malformed JSON, oversized JWT, and pathological claim collections.

### 12.3 PostgreSQL integration tests

Exercise real PostgreSQL 18 and 19 connections with:

- `map=` and `pg_ident.conf` success/failure;
- HBA role and database restrictions;
- multiple issuers and identity collision attempts;
- TLS-required connections;
- provider key rotation;
- discovery/JWKS outage and recovery;
- reload of validator settings;
- delegated mapping, once implemented, including attempts to assume superuser and unrelated roles.

### 12.4 Provider interoperability

Run versioned test suites against supported Keycloak, Auth0, and Entra configurations. Capture sanitized token shapes and metadata as fixtures so core validation remains testable offline.

### 12.5 Fuzzing and analysis

- Fuzz JWT, JWK, discovery JSON, scope, and identity parsing.
- Run compiler sanitizers where PostgreSQL integration permits.
- Run static analysis and dependency vulnerability scanning.
- Review all PostgreSQL error/`longjmp`, allocation, and memory-boundary code manually.

### 12.6 Provisional test implementation architecture

Use a hybrid test architecture unless implementation experience shows a better fit:

- native C unit tests for validation policy, parsing boundaries, cache state, identity construction, and PostgreSQL ABI behavior;
- Python with `pytest` to orchestrate PostgreSQL instances, provider behavior, connection attempts, logs, and PostgreSQL 18/19 parametrization;
- a small C/libpq test client for injecting bearer tokens through PostgreSQL's OAuth hook API when Python drivers do not expose the required hook reliably;
- a local Python mock authorization server for deterministic discovery, JWKS, rotation, redirect, timeout, malformed-response, and outage scenarios;
- native fuzz targets for parsers and validation entry points.

The default suite must run offline and deterministically. Tokens should be passed to test clients through a protected pipe or temporary file rather than command-line arguments. Live-provider tests remain separate and opt-in. Treat `pytest` and any Python driver as provisional dependencies until their versions, licenses, maintenance, and exact OAuth/libpq behavior have been reviewed.

## 13. Documentation requirements

Operator documentation must explain:

- that the token is an OAuth access token, not an ID token;
- issuer, audience, and scope as distinct controls;
- how to configure the provider to issue a PostgreSQL audience;
- why `openid` is not a sufficient PostgreSQL authorization scope;
- the exact `authn_id` format;
- safe `pg_hba.conf` and `pg_ident.conf` examples;
- why HBA roles should be restricted rather than broadly using `all`;
- that delegated identity mapping is unsupported until role-aware authorization exists;
- TLS, token replay, lifetime, revocation, cache, and outage considerations;
- provider-specific prerequisites and limitations;
- safe upgrade and rollback procedures.

## 14. Proposed milestones

### Milestone 1: strict offline JWT validation

- Exact issuer and discovery-issuer validation.
- Mandatory configured audience validation.
- Required `exp` and stable subject.
- Token-type enforcement through a strict/profile setting.
- Local asymmetric algorithm allowlist.
- Hardened JWKS selection and one controlled rotation refresh.
- Normal `pg_ident.conf` mapping only.
- Complete negative unit-test corpus.

### Milestone 2: operational hardening

- Shared metadata/JWKS cache redesign.
- Response limits, timeouts, TLS and redirect policy.
- Stable diagnostic codes, metrics, and sanitized audit logs.
- PostgreSQL integration suite and provider fixtures.
- Configuration reload and cache invalidation behavior.

### Milestone 3: supported provider profiles

- Keycloak profile and end-to-end tests.
- Auth0 profile and end-to-end tests.
- Entra profile with tenant and identity guidance.
- Compatibility matrix and version policy.

### Milestone 4: delegated authorization

- Explicit role/group/entitlement policy model.
- Use of the callback's requested PostgreSQL `role`.
- Administrative-role safeguards.
- Comprehensive privilege-escalation tests.
- Documentation clearly separating normal mapping from delegation.

### Milestone 5: introspection and revocation options

- RFC 7662 mode.
- Secure credential handling.
- Bounded caching and fail-closed outage behavior.
- Operational guidance comparing offline JWT and online introspection.

## 15. Initial acceptance criteria

The validator is ready for an initial security review when all of the following are true:

- A correctly signed token for another audience is rejected.
- An ID token signed by the trusted issuer is rejected as an access token.
- Tokens missing `exp`, `aud`, or the configured identity are rejected.
- Only administrator-allowed asymmetric algorithms are accepted.
- A rotated signing key can be adopted without allowing attacker-driven refresh storms.
- A valid token cannot assume a PostgreSQL role without a matching usermap entry.
- Delegated mapping is rejected or documented as unsupported until role-aware policy exists.
- No token or secret appears in normal, debug, or error logs.
- Network failures and cache behavior are deterministic and documented.
- Negative tests cover the threat model and run in continuous integration.

## 16. Decisions needed before implementation

Dependency direction is recorded in
[`docs/adr/0001-jose-json-crypto-http-dependencies.md`](docs/adr/0001-jose-json-crypto-http-dependencies.md).
It remains gated by the ADR's validation spike and does not resolve the product
policy decisions below.

1. Choose the canonical PostgreSQL audience identifier and how clients request it from each provider.
2. Decide whether the first release is strict RFC 9068 only or includes explicit compatibility profiles.
3. Initial decision: use
   `v1.<base64url(issuer)>.<base64url(configured-stable-claim)>`, limited to
   1024 ASCII bytes. Encoding-version changes require migration guidance for
   every affected `pg_ident.conf` usermap.
4. Decide whether an empty required-scope configuration is prohibited in production mode.
5. Initial decision: support RS256 with an explicit minimum of at least 2048
   bits and a bounded deployment maximum, and ES256 only with a validated
   P-256 public point. Adding algorithms or weakening these bounds requires a
   reviewed policy and negative tests.
6. Define cache freshness and provider-outage policy.
7. Decide whether delegated authorization belongs in the same module or a later policy-focused module.
8. Decide whether introspection is required for the intended production threat model.
