# ADR 0002: Metadata/JWKS cache and provider-outage policy

- Status: Accepted for Milestone 1 implementation
- Date: 2026-08-08
- Scope: Strict offline JWT validation for PostgreSQL 18 and 19

## Context

The validator needs authorization-server metadata and public signing keys, but
making both HTTP requests for every database connection would add provider
latency and availability to every authentication attempt. It would also turn
attacker-controlled connection attempts and unknown `kid` values into outbound
request amplification.

Conversely, retaining signing keys indefinitely can continue trusting removed
or compromised keys. A provider outage must never turn an unverifiable token
into an accepted token, and configuration reloads must not leave cache entries
valid under a changed trust policy.

PostgreSQL backends are separate processes. A process-local cache would provide
little benefit during connection authentication, while requiring the validator
to be listed separately in `shared_preload_libraries` would add an unnecessary
operator contract. PostgreSQL 18 provides `GetNamedDSMSegment()` for bounded
shared state created and initialized safely after server startup.

RFC 9111 distinguishes fresh and stale responses, prohibits stale reuse unless
explicitly permitted, and requires `must-revalidate` to be honored. RFC 8414
requires HTTPS metadata/JWKS locations and exact issuer validation remains an
independent trust check; HTTP cache metadata never establishes issuer trust.

## Decision

### Shared cache shape

Use one fixed-size named dynamic shared-memory segment per PostgreSQL instance.
Do not require `shared_preload_libraries`.

The segment contains a fixed maximum number of entries and fixed-size bounded
metadata. Response bodies are stored only after transport bounds, strict JSON
parsing, exact issuer validation, URL policy, and JWKS validation succeed. No
bearer token, JWT payload, authenticated identity, secret, or complete claims
are cached.

Each entry is keyed by all trust-relevant inputs:

- exact issuer;
- exact metadata URL;
- exact JWKS URL returned by validated metadata;
- named provider policy/profile;
- allowed algorithms and key constraints;
- discovery/JWKS host policy; and
- all remaining trust-policy limits and authorization inputs.

The bounded canonical key material is versioned and compared in full before
reuse; an implementation may use a hash only as an accelerator, never as
equality. Cache capacity, response sizes, URLs, key counts and key identifiers
remain bounded. When capacity is exhausted, replacement is
deterministic and prefers an expired least-recently-used entry that is not being
refreshed. If no safe victim exists, the request fails closed.

### Freshness

Metadata and JWKS freshness are tracked separately using monotonic time for
elapsed intervals. Wall-clock HTTP dates may inform freshness but never replace
monotonic expiry inside a running server.

- Honor valid `Cache-Control: max-age` and `Expires` values, capped by a local
  administrator maximum.
- Honor `no-store` by not retaining the response beyond the current validation.
- Honor `no-cache` and `must-revalidate` by requiring successful revalidation
  before reuse once fresh use ends.
- Treat malformed or conflicting freshness directives conservatively as an
  immediately stale response. The current validated response may still serve
  its initiating request, but it is not reusable without revalidation.
- When no usable explicit freshness exists, apply a bounded local default TTL.
- Never extend a provider's shorter explicit freshness lifetime up to the local
  default. The local setting is a fallback and upper bound, not a minimum.

Initial configuration contract:

```conf
pg_oauth_validator.metadata_default_ttl = '5min'
pg_oauth_validator.jwks_default_ttl = '5min'
pg_oauth_validator.cache_max_ttl = '1h'
pg_oauth_validator.jwks_stale_grace = '0s'
pg_oauth_validator.unknown_kid_refresh_cooldown = '30s'
pg_oauth_validator.cache_max_entries = 32
```

The operator-facing ranges and reload contexts are:

| Setting | Range | Context |
|---|---:|---|
| `metadata_default_ttl` | 1 second–1 hour | reload |
| `jwks_default_ttl` | 1 second–1 hour | reload |
| `cache_max_ttl` | 1 second–24 hours | reload |
| `jwks_stale_grace` | 0–1 hour | reload |
| `unknown_kid_refresh_cooldown` | 1 second–5 minutes | reload |
| `cache_max_entries` | 8–256 | restart |

`cache_max_ttl` must not be shorter than either fallback TTL. Invalid or
internally inconsistent values fail closed rather than being silently clamped.
Every reloadable value participates in canonical cache-key separation. Cache
capacity determines shared-memory layout and therefore cannot change on
reload.

`jwks_stale_grace = 0s` is the fail-closed default. An operator may explicitly
choose a short nonzero value, bounded to at most one hour, as an availability
tradeoff. Metadata has no stale grace in Milestone 1: expired metadata must be
successfully refreshed before a changed or newly discovered JWKS URL can be
trusted.

### Outage behavior

On metadata or JWKS retrieval failure:

1. A still-fresh, policy-matching cached entry may be used.
2. An expired metadata entry is not used to discover or authorize a JWKS URL.
3. An expired JWKS entry may be used only when `jwks_stale_grace` is nonzero,
   the entry is within that bound, the response did not require revalidation,
   and every trust-policy input still matches exactly.
4. Otherwise validation fails closed with a stable, non-sensitive diagnostic.

Stale-key use never skips signature, issuer, audience, token type, expiry,
scope, or identity validation. It only supplies previously validated public
verification keys. Successful refresh atomically replaces an entry; a failed
refresh never destroys an otherwise permitted fresh or stale entry.

### Rotation and request suppression

A JWT referencing an unknown `kid` may cause at most one JWKS refresh during
that validation attempt. Refresh is additionally limited by the exact issuer
entry's monotonic cooldown. Attacker-selected `kid` values are not part of the
cache key and cannot create cache entries or bypass the cooldown.

Only one backend marks an entry as refreshing. The shared lock protects brief
lookup, copy, state-transition and replacement operations only. It is released
before DNS, connection, TLS or HTTP work. Other backends may use an eligible
fresh/stale snapshot; if none exists, they fail closed rather than waiting on a
network operation while holding or polling a PostgreSQL lock.

Signature failure with a known key does not automatically trigger refresh in
Milestone 1. This avoids turning arbitrary invalid signatures into outbound
traffic. A future change requires a separate threat analysis and tests.

### Reload and invalidation

Every lookup recomputes the versioned canonical key from the complete effective
trust policy. A mismatch makes the entry ineligible immediately, even if its
TTL has not expired. Reload changes to issuer, metadata URL, audience/profile,
algorithms, key limits, TLS/host policy, or stale policy therefore cannot reuse
an entry created under the old contract.

Reducing cache bounds or changing the cache's shared-memory layout requires a
PostgreSQL restart. Reloadable policy changes use canonical-key invalidation and
lazy bounded replacement; raw old entries are never accepted under the new
canonical key.

### Diagnostics and observability

Expose aggregate bounded counters for fresh hit, refresh, refresh suppression,
stale use, eviction and failure. Logs use stable codes and exact configured
issuer/profile identifiers only where PostgreSQL log policy permits. Never log
tokens, signatures, JWK private material or response bodies.

## Consequences

- Normal authentication does not contact the provider while validated cache
  data is fresh.
- Default outage behavior remains fail closed after freshness expires.
- Operators who enable stale-key grace explicitly accept a bounded key-removal
  delay and must document that risk.
- The cache cannot silently bridge a changed trust configuration.
- Fixed-size shared memory makes resource use reviewable and prevents
  attacker-driven allocation growth.
- A cold cache can reject concurrent connections while one backend refreshes;
  this favors bounded failure over lock-held network waits or request storms.

## Rejected alternatives

### Fetch metadata and JWKS for every connection

Rejected because it makes provider availability part of every login and permits
connection attempts to amplify outbound requests.

### Process-local cache

Rejected because authentication backends do not provide a durable cache across
new connections.

### Unbounded or indefinite last-known-good keys

Rejected because removed keys and broadened old policies could remain trusted
without a reviewed time limit.

### Stale reuse enabled by default

Rejected because availability should not silently extend trust in keys the
issuer may have removed. The operator must choose and bound this tradeoff.

### Network access while holding an LWLock

Rejected because provider or DNS latency would block unrelated PostgreSQL
backends and create a denial-of-service boundary.

### libjwt's remote JWKS cache

Rejected by ADR 0001 because it cannot own this validator's exact issuer,
reload, host, resource-bound and outage policy.

## Implementation gates

The PostgreSQL-independent cache state machine now covers exact bounded keys,
fresh/stale boundaries, refresh ownership tokens, unknown-`kid` cooldown,
failed-refresh preservation, revalidation-required stale denial, `no-store`
removal, deterministic expired-LRU eviction, fixed capacity and injected time.
The cache state machine now also stores a fixed maximum 64 KiB validated
payload per entry and copies it into caller-owned memory under the adapter's
future lock. Refresh completion publishes payload and freshness atomically;
failed or obsolete refresh owners cannot replace an eligible payload.
The HTTP freshness boundary parses bounded `Cache-Control`, `Date`,
`Expires`, and `Age` values, applies local TTL caps, and preserves `no-store`,
`no-cache`, and `must-revalidate` semantics. PostgreSQL shared-memory/lock
integration now uses a versioned named DSM segment and an embedded LWLock,
without requiring `shared_preload_libraries`. The adapter copies payloads and
performs state transitions under the lock but returns refresh ownership before
any caller can start network work. Its PostgreSQL 18 and 19 API branches build
separately and segment attachment runs in both real OAuth integration suites.
A versioned, length-prefixed canonical cache-key builder now separates metadata
from JWKS resources and includes every current trust-relevant policy field;
tests mutate each field independently and cover boundary ambiguity and size
failure. Issuer-resource retrieval now uses the canonical keys, validated
payload publication, HTTP freshness, single-owner refresh, unknown-key
cooldown, and explicitly bounded stale-JWKS fallback through a cache operations
interface implemented by the shared PostgreSQL adapter. Cross-backend
integration tests now prove single-owner refresh suppression, fail-closed cold
lookup during an in-flight refresh, prompt competing-backend return while the
owner simulates slow network work outside the lock, and post-publication
visibility from another backend. Callback-bound HTTPS validation and normal
`pg_ident.conf` mapping now exercise the same shared cache in PostgreSQL 18 and
19 integration tests.

The PostgreSQL callback authorization path is gated by tests that prove:

- fixed capacity and deterministic safe eviction;
- exact composite-key and policy-field separation;
- fresh, expired, `no-store`, `no-cache`, `must-revalidate`, malformed and
  capped HTTP freshness behavior;
- default fail-closed outage and explicitly bounded stale use;
- unknown-`kid` single refresh and per-issuer cooldown under concurrency;
- failed refresh preservation of eligible cached keys;
- policy reload invalidation for every trust-relevant field;
- no PostgreSQL lock held across mocked slow network operations;
- PostgreSQL 18 and 19 shared-segment initialization and concurrent access;
- bounded diagnostics and bearer-token/response redaction; and
- deterministic tests using injected monotonic time rather than sleeps.

## References

- PostgreSQL 18, C-language functions: dynamic shared memory and LWLocks
- PostgreSQL 18/19, OAuth validator callbacks
- RFC 8414, OAuth 2.0 Authorization Server Metadata
- RFC 9111, HTTP Caching
- RFC 9068, JWT Profile for OAuth 2.0 Access Tokens
