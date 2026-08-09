# OAuth provider compatibility notes

Status: research draft, not a support statement

Last reviewed: 2026-08-08

## Purpose and scope

This document evaluates likely OAuth/OIDC authorization servers for use with
`pg_oauth_validator`. It records expected operator configuration, token-shape
differences, and incompatibilities with the current design. It is deliberately
separate from the product contract in [`FEATURES.md`](FEATURES.md). A provider
listed here is not supported until its named profile and interoperability tests
are implemented and the feature specification says that it is supported.

The important object is an OAuth 2.0 **access token** issued for PostgreSQL as a
resource server. OIDC discovery is used to locate issuer metadata and signing
keys, but an OIDC ID token must never be accepted as a database credential.

This review covers the providers prioritized in the design plan:

1. Microsoft Entra ID
2. Okta
3. Auth0
4. Keycloak
5. Google Identity
6. Ping Identity / ForgeRock
7. Amazon Cognito
8. ZITADEL
9. FusionAuth
10. a generic RFC 9068 authorization server

Products and hosted offerings change. Before declaring a profile supported,
capture sanitized metadata and token fixtures from a named product/version and
repeat the checks in this document against current official documentation.

## Current implementation and compatibility limits

The module implements the strict Milestone 1 validation path. The common
configuration below is usable for RFC 9068-shaped access tokens, but every
provider still requires an interoperability test before this document may call
it supported. Named provider profiles remain future configuration.

The policy builder currently enforces:

- one global comma-separated `pg_oauth_validator.audiences` setting;
- `pg_oauth_validator.required_token_type = 'at+jwt'` only;
- `RS256` and/or `ES256` only;
- `sub` as the default authenticated identity claim;
- direct identity matching by default, with optional issuer-qualified usermaps;
- opt-in exact string-array role authorization when HBA delegation is enabled;
- a nonempty required scope on every matching OAuth HBA rule;
- only the unnamed/default policy (named provider profiles do not exist yet).

Consequently, only an RFC 9068-shaped access token can satisfy the current
strict path. Most providers below require a named compatibility profile. The
current global audience setting is also unsuitable for cleanly isolating
multiple issuers with different PostgreSQL audience values. PostgreSQL 19's
`validator.policy` selector is reserved for that future policy store and only
`default` is currently accepted. PostgreSQL 18 uses the same global strict
policy but has no HBA policy selector.

## Common target configuration

Use an exact, administrator-chosen issuer and a dedicated PostgreSQL audience
and scope. Placeholder values are shown in angle brackets and must not be copied
literally.

```conf
# postgresql.conf -- current strict settings
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
pg_oauth_validator.allowed_algorithms = 'RS256'
pg_oauth_validator.required_token_type = 'at+jwt'
pg_oauth_validator.identity_claim = 'sub'
pg_oauth_validator.identity_format = 'issuer_qualified'
pg_oauth_validator.clock_skew = '60s'
pg_oauth_validator.max_token_size = '16kB'
```

```conf
# pg_hba.conf -- PostgreSQL 19 strict default policy
hostssl appdb +oauth_login 10.0.0.0/8 oauth \
    issuer="<exact-provider-issuer>" \
    scope="connect:postgres" \
    validator=pg_oauth_validator \
    validator.policy=default \
    map=oauth
```

```conf
# pg_ident.conf -- v1.Base64URL(issuer).Base64URL(stable claim)
oauth  "<encoded-authenticated-identity>"  app_user
```

Production rules should use `hostssl` and restrict database and role targets.
Delegated mode accepts only an explicitly configured string-array claim with
exact matching; provider group transformations remain unsupported.

## Compatibility summary

| Provider/profile | Offline JWT for a custom PostgreSQL resource | Main mismatch with strict `rfc9068` plan | Expected profile |
|---|---:|---|---|
| Auth0 RFC 9068 API | Yes | Must opt into RFC 9068 dialect; default dialect is `JWT` | `auth0-rfc9068` |
| Generic RFC 9068 AS | Yes | Conformance and exact token shape still require testing | `rfc9068` |
| PingOne custom resource | Likely | `at+jwt` header is optional and must be enabled | `pingone` or `rfc9068` after tests |
| Microsoft Entra v2 | Yes | `typ=JWT`, scopes in `scp`, tenant/version rules | `entra-v2` |
| Okta custom authorization server | Yes | custom server required; `typ=JWT`/`scp` conventions | `okta-custom-as` |
| Keycloak 26.2+ RFC 9068 client | Yes | `at+jwt` is opt-in; audience and subject need explicit mappers | strict default |
| Amazon Cognito user pool | Yes | access-token marker is `token_use=access`; `aud` is conditional | `cognito-user-pool` |
| ZITADEL JWT access token | Conditional | can issue JWT or opaque tokens; audience is project-oriented | `zitadel-jwt` |
| FusionAuth | Yes | typically `typ=JWT`; custom token marker/claims; signing policy varies | `fusionauth` |
| Google Accounts OAuth | No for this design | tokens target Google APIs; no custom PostgreSQL resource-server model | unsupported; broker through another AS |

“Likely” and “conditional” are research outcomes, not support claims. Each must
be promoted only after a live, versioned interoperability fixture passes all
negative tests, especially ID-token substitution.

## 1. Microsoft Entra ID (`entra-v2`)

### Provider setup

- Register PostgreSQL as a single-tenant Web API unless multitenancy is an
  explicit product requirement.
- Set `requestedAccessTokenVersion` to `2` on the resource application.
- Expose a delegated scope such as `connect:postgres` and grant the client that
  scope. For application-only tokens, Entra emits application `roles` rather
  than `scp`; the first profile should therefore accept delegated user tokens
  only unless machine identity receives a separate policy design.
- Use the tenant-specific v2 authority. Do not configure `common`,
  `organizations`, or a token-supplied tenant as the trusted issuer.

### Expected validator configuration

```conf
pg_oauth_validator.audiences = '<resource-application-client-id-guid>'
pg_oauth_validator.allowed_algorithms = 'RS256'
# Future named profile overrides strict at+jwt and reads scp.
```

```conf
issuer="https://login.microsoftonline.com/<tenant-id>/v2.0"
scope="connect:postgres"
validator.policy="entra-v2"
```

The v2 access-token audience is the Web API's client ID GUID, not necessarily
its Application ID URI. Use the exact `issuer` returned by tenant-specific
discovery rather than constructing or normalizing it casually.

### Oddities and required profile behavior

- Entra documents the access-token header `typ` as always `JWT`, so it fails the
  current mandatory `at+jwt` check.
- Delegated scopes are a space-delimited `scp` string. Application-only tokens
  normally use a `roles` array and have no `scp`.
- `sub` is pairwise per application and tenant. `oid` plus `tid` can provide a
  tenant-stable object identity, but choosing it would change the generic
  `issuer|sub` contract and requires a deliberate profile decision.
- `preferred_username`, `email`, `name`, and `upn` are mutable/display claims
  and must not be the default identity.
- Group claims can be replaced by an overage indication for highly connected
  users. Groups must not drive Milestone 1 authorization in any event.
- A multitenant application introduces multiple exact issuers. Supporting it
  safely requires explicit tenant allowlisting and per-issuer cache separation,
  not wildcard issuer matching.

Official references: [access-token claims](https://learn.microsoft.com/en-us/entra/identity-platform/access-token-claims-reference),
[claim validation and v1/v2 audiences](https://learn.microsoft.com/en-us/entra/identity-platform/claims-validation),
and [OIDC discovery](https://learn.microsoft.com/en-us/entra/identity-platform/v2-protocols-oidc).

## 2. Okta (`okta-custom-as`)

### Provider setup

- Create or use a **custom authorization server**. Do not use the org
  authorization server: its access tokens are for Okta and Okta says custom
  applications should not validate them.
- Set the authorization-server audience to a dedicated value such as
  `https://postgres.example.internal/`.
- Define `connect:postgres`, create an access policy/rule that grants it only to
  intended clients and users, and request that scope from the client.
- Confirm that API Access Management licensing is available in the production
  organization.

### Expected validator configuration

```conf
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
pg_oauth_validator.allowed_algorithms = 'RS256'
```

```conf
issuer="https://<okta-domain>/oauth2/<authorization-server-id>"
scope="connect:postgres"
validator.policy="okta-custom-as"
```

### Oddities and required profile behavior

- The org and custom authorization servers have different issuers and trust
  purposes. Accidentally accepting org-server tokens would cross a resource
  boundary.
- Okta examples use `typ=JWT` and an `scp` array rather than RFC 9068's
  `at+jwt` plus space-delimited `scope`; both details need fixture confirmation
  and an issuer-bound profile.
- Custom claims and scopes are omitted from discovery metadata, so support
  cannot infer their existence from discovery.
- Token encryption can be enabled. Encrypted/nested access tokens are outside
  Milestone 1 and must be rejected rather than partially processed.
- A client restriction (`cid`) may be useful hardening, but it is separate from
  audience and is not currently planned configuration.

Official references: [authorization-server types and issuer formats](https://developer.okta.com/docs/concepts/auth-servers/),
[OAuth claims and `scp`](https://developer.okta.com/docs/concepts/oauth-claims/),
and [access-token validation](https://developer.okta.com/docs/guides/validate-access-tokens/nodejs/main/).

## 3. Auth0 (`auth0-rfc9068`)

### Provider setup

- Register a custom API whose Identifier is the exact PostgreSQL audience,
  for example `https://postgres.example.internal/`.
- Select the RFC 9068 access-token profile (`rfc9068_profile`, or the RBAC
  variant only if separately needed).
- Configure RS256 signing, define `connect:postgres`, authorize the client for
  that scope, and request both the API `audience` and scope.
- Choose either the Auth0 tenant domain or a custom domain and use it
  consistently. The domain used to request the token affects `iss`.

### Expected validator configuration

```conf
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
pg_oauth_validator.allowed_algorithms = 'RS256'
pg_oauth_validator.required_token_type = 'at+jwt'
```

```conf
issuer="https://<tenant-or-custom-domain>/"
scope="connect:postgres"
validator.policy="auth0-rfc9068"
```

### Oddities and required profile behavior

- Auth0's default API token dialect uses `typ=JWT`; only the opt-in RFC 9068
  profile supplies `typ=at+jwt`.
- A token obtained without a custom API audience can be opaque and intended
  only for `/userinfo`; reject it.
- Requesting `openid` with a custom API audience can make `aud` an array that
  also contains the `/userinfo` audience. Exact membership matching is needed.
- The standard `sub` is stable within the tenant and may itself contain `|`.
  The implemented versioned, component-wise Base64URL identity encoding is
  unambiguous and does not rely on a delimiter appearing outside the encoding.
- Auth0's default custom-API lifetime can be long. Operators should configure a
  short database-appropriate token lifetime; the validator still enforces only
  the signed `exp` unless a maximum-age policy is added.

Official references: [access-token profiles](https://auth0.com/docs/secure/tokens/access-tokens/access-token-profiles),
[requesting a custom API audience](https://auth0.com/docs/secure/tokens/access-tokens/get-access-tokens),
and [custom API access tokens](https://auth0.com/docs/secure/tokens/access-tokens).

## 4. Keycloak (`keycloak`)

### Provider setup

- Use one exact realm issuer, normally
  `https://<host>/realms/<realm>`.
- Represent PostgreSQL as a resource-server client or custom audience.
- Add a hardcoded Audience protocol mapper for
  `https://postgres.example.internal/` and enable it for access tokens.
- Define a client scope `connect:postgres`, attach it only where appropriate,
  and request it during authorization.
- On Keycloak 26.2 or later, enable **Use "at+jwt" as access token header
  type** for the OAuth client.
- Use RS256 initially and keep realm signing-key rotation represented in JWKS.

### Expected validator configuration

```conf
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
pg_oauth_validator.allowed_algorithms = 'RS256'
```

```conf
issuer="https://<host>/realms/<realm>"
scope="connect:postgres"
validator.policy="default"
```

### Oddities and required profile behavior

- Keycloak 26.2 and later provide **Use "at+jwt" as access token header type**
  under the client's fine-grained OpenID Connect configuration. It is disabled
  by default for compatibility and must be enabled. In realm JSON the client
  attribute is `"access.token.header.type.rfc9068": "true"`.
- The pinned Keycloak 26.5.0 interoperability suite confirms that enabling
  this option produces an RS256 `typ=at+jwt` access token with the configured
  PostgreSQL audience, requested `connect:postgres` scope, and explicit
  subject. The standards-based default validator accepts it without a
  Keycloak-specific profile.
- The suite proves that a same-realm ID token, missing scope, wrong audience,
  wrong issuer, and unmapped administrative-role request are rejected. Neither
  access nor ID tokens reach logs or client errors.
- The suite creates a new active RSA signing key, proves that a token signed by
  the new key is accepted, and proves that a token signed by the previous key
  remains accepted while Keycloak continues to publish that key in JWKS.
- The suite then stops the Keycloak container. Authentication fails closed
  while discovery and JWKS are unavailable and succeeds again after Keycloak
  restarts. Keycloak 26.5.0 returns `Cache-Control: no-cache` for JWKS and
  `no-cache, must-revalidate, no-transform, no-store` for discovery, so the
  validator must revalidate rather than bridge this outage with stale data.
  Provider-independent stale-cache and unknown-key refresh suppression remain
  covered by deterministic local tests.
- The client that requested a token is not necessarily present in `aud`.
  Audience Resolve adds audiences based on client roles; a hardcoded mapper is
  clearer for a dedicated PostgreSQL resource.
- Realm roles and client roles appear in nested `realm_access` and
  `resource_access` structures. They are not scopes and must not become
  PostgreSQL roles automatically.
- Keycloak can issue lightweight access tokens intended for introspection.
  They are outside the offline JWT profile unless their exact signed shape is
  separately specified and tested.
- Realm issuer paths and trailing slashes are significant; use discovery's
  exact issuer value.

Official references: [OIDC endpoints](https://www.keycloak.org/securing-apps/oidc-layers),
[audience and role mapping](https://www.keycloak.org/docs/latest/server_admin/#_audience),
and [the Keycloak RFC 9068 header-type change](https://github.com/keycloak/keycloak/pull/37573/files).
The reproducible realm and assertions live under
[`tests/interop/keycloak`](tests/interop/keycloak).

## 5. Google Identity

### Compatibility decision

Direct Google Accounts OAuth access tokens should be unsupported in Milestone
1. Google's public OAuth authorization server issues access tokens for Google
APIs and controls access through Google API scopes. It does not provide the
general custom authorization-server/resource registration needed to mint a
signed JWT access token with a PostgreSQL audience and `connect:postgres`.

Google Sign-In does provide an OIDC ID token, but accepting that ID token would
directly violate the validator's access-token-only invariant. Google Cloud
Identity Platform likewise describes its signed JWT as an **ID token** used to
identify a signed-in user, not as a custom PostgreSQL access token.

### Safe integration shape

Use Google as an upstream identity provider to Auth0, Okta, Keycloak, PingOne,
ZITADEL, or another authorization server. That broker must mint its own access
token for the PostgreSQL resource. Configure and trust only the broker's exact
issuer; do not trust the upstream Google ID/access token at PostgreSQL.

There is therefore no direct expected validator configuration. A brokered
deployment uses the broker section in this document.

Official references: [Google OAuth tokens target Google APIs](https://developers.google.com/identity/protocols/oauth2)
and [Google Cloud Identity Platform token types](https://cloud.google.com/identity-platform/docs/concepts-manage-users#auth_tokens).

## 6. Ping Identity / ForgeRock

“Ping Identity / ForgeRock” is a product family, not one token profile. The
initial scope here is **PingOne custom resources**. PingFederate and PingOne
Advanced Identity Cloud (the ForgeRock lineage) require separate named profiles
and versioned fixtures rather than inheriting PingOne behavior.

### PingOne provider setup

- Add a custom resource for PostgreSQL with audience
  `https://postgres.example.internal/`.
- Add `connect:postgres` to that resource and allow the OIDC application to
  request it.
- On the application, enable inclusion of the `typ` header in access tokens;
  PingOne documents that this emits `typ=at+jwt` for custom resources.
- Map an immutable user identifier to `sub` and mark it required for interactive
  flows. Configure an asymmetric signing algorithm supported by the validator.

### Expected validator configuration

```conf
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
pg_oauth_validator.allowed_algorithms = 'RS256'
pg_oauth_validator.required_token_type = 'at+jwt'
```

```conf
issuer="<exact-issuer-from-PingOne-discovery>"
scope="connect:postgres"
validator.policy="pingone"
```

### Oddities and required profile behavior

- `typ=at+jwt` is an application option, not a safe assumption about every
  Ping-issued token.
- A custom-resource audience is optional in PingOne and otherwise defaults to
  the resource name. Set it explicitly.
- Client-credentials tokens can omit `sub` unless resource mapping requires it.
  Machine identities therefore need a separate policy contract.
- Ping products expose extensive custom attribute and authorization mapping.
  No custom permission or role claim is trusted by Milestone 1.
- Do not apply a PingOne profile to PingFederate or Advanced Identity Cloud
  solely because all products share a vendor.

Official references: [creating a PingOne custom resource](https://docs.pingidentity.com/pingone/applications/p1_adding_custom_resource.html),
[enabling `at+jwt`](https://docs.pingidentity.com/pingone/applications/p1_edit_application_native.html),
and [resource scopes](https://docs.pingidentity.com/pingone/applications/p1_resources.html).

## 7. Amazon Cognito (`cognito-user-pool`)

### Provider setup

- Use a Cognito **user pool**, not an identity pool.
- Add a domain so clients use the OAuth authorization server rather than only
  `InitiateAuth`/`AdminInitiateAuth` SDK flows.
- Define a resource server and custom scope. Cognito scope names are normally
  `<resource-server-identifier>/connect:postgres`; place that exact emitted
  string in the HBA `scope` option.
- Request resource binding with
  `resource=https://postgres.example.internal/` so the access token contains
  the required `aud` claim. This is available for managed-login authorization
  code/implicit flows and is not available in Cognito SDK authentication flows.

### Expected validator configuration

```conf
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
pg_oauth_validator.allowed_algorithms = 'RS256'
```

```conf
issuer="https://cognito-idp.<region>.amazonaws.com/<user-pool-id>"
scope="<resource-server-identifier>/connect:postgres"
validator.policy="cognito-user-pool"
```

### Oddities and required profile behavior

- Cognito's access-token header documents `kid` and `alg=RS256`, but not an RFC
  9068 `typ=at+jwt`. Access tokens instead contain `token_use=access`; ID tokens
  contain `token_use=id`. A named profile must require this distinction.
- Older/unbound access tokens use `client_id` rather than `aud`. The validator's
  audience invariant means these tokens must be rejected; it must not silently
  treat `client_id` as audience.
- Cognito signs access and ID tokens with different keys, which is useful
  defense in depth but does not replace checking `token_use`.
- `InitiateAuth` tokens contain only the reserved
  `aws.cognito.signin.user.admin` scope and cannot satisfy a dedicated database
  scope.
- Token revocation does not invalidate an already issued JWT under purely
  offline validation. This is the normal bearer-token limitation documented in
  the project threat model.

Official references: [Cognito access-token claims](https://docs.aws.amazon.com/cognito/latest/developerguide/amazon-cognito-user-pools-using-the-access-token.html),
[resource servers and resource binding](https://docs.aws.amazon.com/cognito/latest/developerguide/cognito-user-pools-define-resource-servers.html),
and [JWT verification](https://docs.aws.amazon.com/cognito/latest/developerguide/amazon-cognito-user-pools-using-tokens-verifying-a-jwt.html).

## 8. ZITADEL (`zitadel-jwt`)

### Provider setup

- Configure the API/resource application to issue **JWT** access tokens; ZITADEL
  can also return opaque bearer tokens, which Milestone 1 cannot validate.
- Represent the PostgreSQL API in its own project and request the documented
  project audience scope, for example
  `urn:zitadel:iam:org:project:id:<project-id>:aud`.
- Define/request a dedicated authorization scope or action that results in an
  exact `connect:postgres`-style access-token scope. Capture the emitted value
  before finalizing HBA configuration.
- Use the exact issuer and JWKS URI from discovery, with asymmetric web keys.

### Expected validator configuration

```conf
# Expected aud is the PostgreSQL API project's ID; verify exact emitted form.
pg_oauth_validator.audiences = '<postgres-project-id>'
pg_oauth_validator.allowed_algorithms = 'RS256'
```

```conf
issuer="<exact-zitadel-issuer>"
scope="<exact-emitted-postgresql-scope>"
validator.policy="zitadel-jwt"
```

### Oddities and required profile behavior

- The same deployment can issue JWT or opaque access tokens. Token format must
  be pinned in provider configuration; opaque tokens remain deferred to an
  introspection milestone.
- Audience selection is project-oriented and requested through a reserved URN
  scope rather than a generic `audience` parameter.
- Do not confuse “JWT Profile” client authentication/authorization grant with
  RFC 9068 access-token formatting. A live fixture must establish the access
  token's JOSE `typ` before deciding whether strict `at+jwt` works.
- ZITADEL role claims are requested through additional URN scopes and can be
  organization/project-specific. They must not become PostgreSQL roles without
  the later delegated-authorization design.
- ZITADEL v4 changed signing-key handling for upgrades from older v3 installs;
  versioned tests must exercise JWKS rotation and upgrade behavior.

Official references: [token introspection and JWT/opaque formats](https://zitadel.com/docs/guides/integrate/token-introspection),
[audience and role scopes](https://zitadel.com/docs/guides/integrate/retrieve-user-roles),
and [OIDC/OAuth endpoints](https://zitadel.com/docs/apis/openidoauth/endpoints).

## 9. FusionAuth (`fusionauth`)

### Provider setup

- Create a tenant with a stable HTTPS issuer and configure an asymmetric JWT
  signing key/algorithm. Do not use an HMAC-signed default for remotely
  validated tokens.
- Create an application/resource representation for PostgreSQL.
- Use an exact PostgreSQL `resource` value if the deployed FusionAuth version
  supports the required resource-indicator flow; otherwise the application
  client ID becomes the audience and must be configured exactly.
- Define `connect:postgres`, select Strict scope handling, reject/remove unknown
  scopes, and request the scope from the client.

### Expected validator configuration

```conf
# Prefer the resource URI; otherwise use the exact FusionAuth application ID.
pg_oauth_validator.audiences = 'https://postgres.example.internal/'
pg_oauth_validator.allowed_algorithms = 'RS256'
```

```conf
issuer="https://<fusionauth-host>/<configured-issuer-path>"
scope="connect:postgres"
validator.policy="fusionauth"
```

### Oddities and required profile behavior

- FusionAuth examples commonly use JOSE `typ=JWT`, not `at+jwt`, and older
  examples use HS256. Both conflict with the current strict policy.
- Recent tokens can contain a custom `tty=at` claim. That is not RFC 9068's
  JOSE header and must be part of a carefully tested access-token/ID-token
  separation rule if used.
- Interactive-user JWT audience defaults to the application `client_id`; with
  `resource` values it can become an array containing both client and resource.
- Client-credentials tokens use entity/permission semantics and have token
  shapes that differ from interactive user tokens. Do not combine both in the
  first profile without separate identity and authorization tests.
- Custom JWT-populate lambdas can arbitrarily change claims. Supported
  interoperability must define the permitted tenant/application configuration,
  not merely the FusionAuth product name.
- OAuth scope management is edition-dependent. Verify licensing and the exact
  server version in deployment guidance.

Official references: [JWT claims and audience behavior](https://fusionauth.io/docs/lifecycle/authenticate-users/login-api/json-web-tokens),
[scope handling](https://fusionauth.io/docs/lifecycle/authenticate-users/oauth/scopes),
and [client-credentials access-token claims](https://fusionauth.io/docs/lifecycle/authenticate-users/oauth/tokens).

## 10. Generic RFC 9068 authorization server (`rfc9068`)

The generic profile is the baseline, not a permissive fallback. A server is
eligible only when a fixture proves all of the following:

- exact configured issuer and matching metadata issuer;
- HTTPS discovery/metadata and JWKS under the validator's URL policy;
- a compact signed JWT access token with JOSE `typ=at+jwt` or
  `application/at+jwt` as allowed by RFC 9068;
- locally permitted asymmetric signing algorithm and unambiguous trusted key;
- mandatory `iss`, PostgreSQL `aud`, `exp`, `iat`, `jti`, `client_id`, and a
  correctly typed stable `sub` for user-bearing tokens;
- a space-delimited `scope` containing the exact required database scope;
- an ID token from the same issuer/client is rejected;
- no token-selected key URLs, normalization, or provider auto-detection.

Expected configuration is the common target configuration with
`validator.policy="rfc9068"`. The authorization server must be configured to
mint the PostgreSQL audience; merely receiving a signed JWT is insufficient.

Primary standards: [RFC 9068](https://www.rfc-editor.org/rfc/rfc9068),
[RFC 8414](https://www.rfc-editor.org/rfc/rfc8414),
[OpenID Connect Discovery](https://openid.net/specs/openid-connect-discovery-1_0.html),
and [JWT Best Current Practices, RFC 8725](https://www.rfc-editor.org/rfc/rfc8725).

## Design gaps exposed by this review

The provider survey does not change the current feature contract, but it makes
these decisions prerequisites for provider implementation:

1. **Named policy storage:** issuer, audience, token-type rule, scope claim,
   algorithms, and identity rule must be bound together per named profile. A
   global audience list can accidentally broaden trust across issuers.
2. **Access-token discrimination:** strict RFC 9068 is straightforward. Every
   `typ=JWT` provider needs a positive, documented access-token marker and an ID
   token substitution test; “ignore `typ`” is not an acceptable profile.
3. **Scope syntax:** support must distinguish RFC `scope` strings, Entra `scp`
   strings, Okta `scp` arrays, and application-only permission/role claims. No
   coercion should occur outside the selected profile.
4. **Machine identities:** `sub` and scope behavior varies for client
   credentials. The initial provider profiles should explicitly reject
   application-only tokens unless their identity and authorization contract is
   separately specified.
5. **Audience request instructions:** RFC 8707 `resource`, Auth0 `audience`,
   Entra resource application scopes, Keycloak mappers, Cognito resource
   binding, and ZITADEL project audience scopes are not interchangeable.
6. **Identity encoding:** an unescaped `issuer|sub` string is ambiguous because
   valid provider subjects can contain `|`. Define a canonical, bounded encoding
   before publishing `pg_ident.conf` examples as stable.
7. **Issuer multiplicity:** Entra multitenancy and hosted/custom domains require
   multiple exact issuer configurations, never wildcards or hostname substring
   detection.
8. **Opaque and encrypted tokens:** Google Accounts, ZITADEL opaque mode, and
   encrypted Okta tokens cannot use Milestone 1 offline JWS validation. Reject
   them until explicit introspection/JWE support exists.
9. **Maximum token age:** provider-configured lifetimes can be much longer than
   desirable for database access. Consider a local maximum `exp - iat` policy in
   addition to checking `exp`.
10. **Versioned interoperability:** product family names are too broad. Test and
    document exact offerings and versions, especially Keycloak, Ping products,
    ZITADEL, Cognito resource binding, and FusionAuth.

## Minimum fixture checklist for a provider profile

Before a profile is promoted into `FEATURES.md`, retain sanitized, deterministic
fixtures for:

- discovery metadata and JWKS, including rotation;
- one accepted user access token for the PostgreSQL audience and scope;
- the same token class with wrong/missing audience and scope;
- an ID token from the same issuer and signing-key environment;
- missing/wrong-type `iss`, `aud`, `exp`, subject, scope, `kid`, and token marker;
- disallowed algorithm, unknown `kid`, malicious `jku`/`x5u`, and oversized
  claims/token;
- provider-specific array/string boundaries and group-overage behavior;
- provider outage, stale JWKS, refresh rate limiting, and redacted diagnostics;
- a real PostgreSQL TLS connection with both successful and denied usermaps.

Live-provider suites must remain opt-in. Sanitized fixtures must contain no
credentials, bearer tokens usable against a real deployment, private keys, or
unsanitized production metadata.
