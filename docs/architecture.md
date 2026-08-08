# Implemented architecture

This document summarizes the implementation currently present in the tree. For
the security contract, see [`../FEATURES.md`](../FEATURES.md). For design
rationale and future work, see
[`../oauth-validator-plan.md`](../oauth-validator-plan.md) and the
[`ADR directory`](adr/).

## Validation path

The exported `_PG_oauth_validator_module_init` function registers one
validation callback. For every token and requested role, the callback:

1. initializes `authorized` to `false`;
2. initializes `authn_id` to `NULL`;
3. reads issuer and required scopes from the matched OAuth HBA rule through an
   isolated PostgreSQL compatibility layer;
4. on PostgreSQL 19, reads the optional policy selected by `validator.policy`;
5. combines those inputs with configured defaults into an immutable validation
   policy;
6. derives the standard OIDC discovery URL from the exact issuer;
7. validates metadata, JWKS, signature, claims, audience, expiry, token type,
   scopes, and stable identity through the shared cache;
8. authorizes the PostgreSQL resource and returns the encoded external
   `authn_id` only after every stage succeeds.

PostgreSQL then applies the HBA `map=` and `pg_ident.conf` entry to the
requested role. The callback does not authorize requested roles itself and
never enables delegated identity mapping.

## Parsing, transport, and key retrieval

Discovery metadata has an independent strict parser. It bounds the JSON
document and URLs, rejects duplicate members, requires exact equality with the
administrator-configured issuer, and returns only an absolute validated
`jwks_uri`. HTTPS is mandatory unless an explicit development-only policy
allows HTTP.

The transport adapter performs bounded JSON GETs with TLS verification, exact
host allowlisting, redirect and implicit-proxy denial, strict timeouts, and
response limits. Issuer-key retrieval fetches only the configured metadata URL,
checks the exact issuer, retrieves JWKS under its own exact-host policy, and
selects one locally permitted key and algorithm. Cache refresh and unknown-key
retry are bounded and preserve typed, non-sensitive failure details.

The validation orchestrator connects envelope parsing, issuer-bound key
retrieval, signature verification, claim and scope policy, and stable identity
construction. Every stage must succeed before the next can establish trust.
Rejected tokens never retain an authenticated identity. The PostgreSQL callback
returns only generic client errors and stable, non-sensitive protected-log
diagnostics.

## Shared metadata and JWKS cache

The fixed-capacity cache state machine is independent of PostgreSQL shared
memory. It uses exact bounded composite keys and payloads, injected monotonic
timestamps, explicit fresh/stale lookup, single refresh ownership, unknown-key
cooldown, failed-refresh preservation, `no-store` removal,
revalidation-required stale denial, and expired least-recently-used eviction
without evicting usable entries.

The state machine performs no allocation, networking, sleeping, or internal
locking. A successful refresh publishes validated bytes and freshness state
together; failed refreshes and stale refresh-owner tokens cannot overwrite a
previously eligible payload.

The HTTP transport preserves only the bounded freshness headers used by this
cache. A strict calculator combines `Cache-Control`, `Date`, `Expires`, and
`Age` using injected response time, caps provider freshness by local policy,
rejects ambiguous or malformed security-relevant values, and propagates
`no-store`, `no-cache`, and `must-revalidate` without treating HTTP freshness as
issuer trust.

The module attaches this state to a versioned, fixed-size named dynamic
shared-memory segment and protects transitions and bounded payload copies with
one PostgreSQL LWLock. Refresh ownership is returned after the lock is released,
so provider network work cannot occur through a lock-holding API. PostgreSQL 18
and 19 have isolated compatibility branches for their different named-DSM
initializer and dynamic-tranche signatures.

Cache lookups use a bounded, versioned canonical key rather than a hash alone.
The binary key length-prefixes every variable field, distinguishes metadata
from JWKS resources, and includes every current trust-relevant policy input. A
changed issuer, URL, audience, scope, algorithm, identity claim, host rule,
validation bound, stale policy, or transport policy therefore cannot reuse an
entry from the previous effective policy.

Issuer-resource retrieval publishes metadata only after exact issuer and URL
validation and publishes JWKS only after bounded parsing and requested-key
selection. Fresh entries avoid HTTP, expired entries elect one refresh owner,
unknown key identifiers obey a per-policy cooldown, and a failed JWKS refresh
may fall back only to an explicitly enabled, still-eligible stale entry.

## Claims and identity

The post-signature claims stage enforces exact issuer and audience, integer
expiry and optional activation/issuance times with bounded skew, a bounded
stable identity claim, and every required HBA scope. The strict default uses
the `scope` claim; provider-specific alternatives such as `scp` require a
future exact-issuer profile.

The final PostgreSQL identity is:

```text
v1.<base64url(issuer)>.<base64url(configured-stable-claim)>
```

Both components use canonical unpadded Base64URL. The complete identity is
limited to 1024 ASCII bytes, and encoding performs no Unicode or URL
normalization. Operational examples are in
[`configuration.md`](configuration.md).

## Integration coverage

A test-only PostgreSQL probe exercises the named DSM cache from independent
server backends. While one backend simulates slow provider work after becoming
refresh owner, another observes a cold miss and refresh suppression promptly;
after publication, a third backend reads the validated payload. The probe is a
separate test library and adds no SQL-callable surface to the production module.

The broader integration suite exercises the callback and shared cache through
real PostgreSQL OAuth connections. See [`development.md`](development.md) for
the supported verification commands.
