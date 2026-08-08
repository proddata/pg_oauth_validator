# Configuration

This document describes the currently implemented operator-facing contract.
[`../FEATURES.md`](../FEATURES.md) remains the authoritative product and
security specification.

## Policy inputs and defaults

The validator reads issuer and required scopes from the matched OAuth HBA rule.
Audience has no safe universal default and must be configured explicitly:

```conf
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
```

The remaining settings have restrictive defaults:

```conf
pg_oauth_validator.allowed_algorithms = 'RS256,ES256'
pg_oauth_validator.required_token_type = 'at+jwt'
pg_oauth_validator.authn_claim = 'sub'
pg_oauth_validator.allowed_jwks_hosts = ''
pg_oauth_validator.ca_file = ''
pg_oauth_validator.clock_skew = '60s'
pg_oauth_validator.max_token_size = '16kB'
pg_oauth_validator.allow_insecure_http = off
pg_oauth_validator.metadata_default_ttl = '5min'
pg_oauth_validator.jwks_default_ttl = '5min'
pg_oauth_validator.cache_max_ttl = '1h'
pg_oauth_validator.jwks_stale_grace = '0s'
pg_oauth_validator.unknown_kid_refresh_cooldown = '30s'
pg_oauth_validator.cache_max_entries = 32
```

An empty audience, empty HBA scope, unsupported or duplicate algorithm, token
type other than `at+jwt` or `application/at+jwt`, unsafe identity-claim name,
invalid additional JWKS hostname, excessive clock skew, or invalid token-size
bound causes validation to fail closed. `cache_max_ttl` must not be shorter
than either fallback TTL; an inconsistent cache policy also fails closed. Only
the `default` named policy exists;
PostgreSQL 19 rejects other `validator.policy` values.

## Metadata and JWKS cache

`metadata_default_ttl` and `jwks_default_ttl` apply only when a successful
provider response has no usable explicit freshness. `cache_max_ttl` caps both
fallback and provider-advertised freshness. The accepted ranges are 1 second
to 1 hour for each fallback and 1 second to 24 hours for the maximum.

`jwks_stale_grace` is an explicit outage-availability tradeoff from zero to one
hour. Its default of zero rejects authentication after JWKS freshness expires.
A nonzero value permits previously validated public keys during that bounded
grace only when the response did not require revalidation. It never bypasses
token signature, claim, scope, identity, or role-mapping validation. Metadata
is never used stale.

`unknown_kid_refresh_cooldown` accepts 1 second to 5 minutes and limits
attacker-triggered JWKS refreshes per canonical issuer policy. `cache_max_entries`
accepts 8 to 256 and bounds the combined metadata/JWKS shared cache. The
default of 32 preserves the original fixed capacity.

## HBA and identity mapping

Production OAuth HBA rules should use `hostssl`; bearer-token authentication
over an unencrypted database connection is not recommended. For PostgreSQL 19:

```conf
hostssl all all 0.0.0.0/0 oauth \
    issuer=https://idp.example.com/ \
    scope="connect:postgres" \
    validator=pg_oauth_validator \
    validator.policy=default \
    map=oauth
```

PostgreSQL 18 uses the `default` policy implicitly because it does not expose
the PostgreSQL 19 named-policy selector.

After complete token validation, the callback returns this external identity:

```text
v1.<base64url(issuer)>.<base64url(configured-stable-claim)>
```

Both components use canonical unpadded Base64URL and the complete value is
limited to 1024 ASCII bytes. Encoding is byte-preserving and performs no
Unicode or URL normalization. For issuer `https://idp.example/realms/acme` and
subject `248289761001`, a literal usermap entry is:

```conf
oauthmap  v1.aHR0cHM6Ly9pZHAuZXhhbXBsZS9yZWFsbXMvYWNtZQ.MjQ4Mjg5NzYxMDAx  app_user
```

PostgreSQL applies the HBA `map=` and `pg_ident.conf` entry to the requested
role. The callback does not authorize requested roles itself and never enables
delegated identity mapping.

## JWKS locations and TLS

JWKS retrieval permits the exact hostname of the trusted discovery endpoint by
default. `allowed_jwks_hosts` is an optional comma-separated list of additional
exact hostnames for deployments whose discovery document deliberately points
at another host. Entries contain hostnames only, without schemes, ports, paths,
wildcards, or credentials. Keep the setting empty unless verified provider
metadata requires an exception.

This transport restriction works the same way on PostgreSQL 18 and 19 and is
independent of PostgreSQL 19's `validator.policy` selector. HTTP redirects are
not followed.

`ca_file` optionally selects a PEM CA bundle for metadata and JWKS HTTPS. An
empty value uses the system trust store. `allow_insecure_http` exists only for
isolated development and is disabled by default.

Do not place credentials, bearer tokens, or other secrets in `ca_file` paths or
any validator GUC. PostgreSQL core can include changed setting names and values
in configuration-reload diagnostics; GUC values are therefore operator-visible
configuration, not secret storage. Protect the referenced CA bundle with normal
filesystem permissions and use a non-sensitive path.

## Reload behavior

All validator GUCs use `PGC_SIGHUP` because the validator is loaded lazily. New backends recompute canonical
cache keys from the complete effective policy after reload. A changed audience,
algorithm, token type, identity claim, host rule, CA path, bound, or transport
policy cannot reuse an entry created under the previous policy.

The fallback TTLs, maximum TTL, stale grace, and unknown-key cooldown also
participate in canonical keys. Reloading any of them makes entries created
under the previous cache policy ineligible immediately. `cache_max_entries`
is accepted from PostgreSQL configuration on reload because the validator is
loaded after server startup, but it determines named shared-memory layout and
therefore requires a PostgreSQL restart to become usable. If it changes during
a reload, cache attachment and authentication fail closed until restart; this
prevents a backend from silently continuing with the old capacity.

The trust-root path is part of the canonical cache key. Replacing the contents
of `ca_file` at the same path is not observable as a GUC change: use a new,
versioned path and reload, or restart PostgreSQL, when rotating that bundle.

## Security constraints

- The accepted credential is an OAuth access token, not an OIDC ID token.
- Audience and required HBA scopes are mandatory.
- HTTPS and certificate verification are required in production.
- `openid` alone is not a sufficient PostgreSQL connection scope.
- A valid token does not permit an identity to assume a role without a matching
  PostgreSQL usermap entry.
- Provider exceptions require a reviewed, explicitly selected profile; they are
  not inferred from issuer hostnames.

See [`../PROVIDER-COMPATIBILITY.md`](../PROVIDER-COMPATIBILITY.md) before
configuring a specific authorization server.
