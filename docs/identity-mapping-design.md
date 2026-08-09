# OAuth identity and PostgreSQL role mapping

Status: proposed design

## Goal

Let operators choose how a validated OAuth identity becomes a PostgreSQL role
without requiring one `pg_ident.conf` entry per user.

Token validation does not change. The validator must still verify the trusted
issuer, signature, audience, expiry, token type, and required scopes before it
returns an identity.

## Two independent settings

Identity handling has two separate choices:

1. **Identity claim:** which string claim identifies the user, such as `sub`,
   `preferred_username`, or an administrator-defined custom claim.
2. **Identity format:** whether the validator returns that claim directly or
   wraps it with the issuer.

Proposed configuration names:

```conf
identity_claim = sub
identity_format = direct        # direct | issuer_qualified
```

The claim must exist and be a non-empty string. Provider profiles may recommend
a claim, but operators remain able to configure it explicitly.

## Direct format

With `identity_format = direct`, the validator returns the selected claim
unchanged as PostgreSQL's authenticated identity.

For this token:

```json
{
  "iss": "https://example.us.auth0.com/",
  "sub": "google-oauth2|108329268577383920408"
}
```

the authenticated identity is:

```text
google-oauth2|108329268577383920408
```

An identically named PostgreSQL role can therefore connect without a usermap.
The role must be quoted when created because its name contains `|`:

```sql
CREATE ROLE "google-oauth2|108329268577383920408" LOGIN;
```

The client requests that exact role name. PostgreSQL permits the connection
only when the requested role exactly equals the identity returned by the
validator.

A more convenient provider claim may instead contain a normal PostgreSQL role
name:

```json
{ "postgres_username": "app_reader" }
```

```conf
identity_claim = postgres_username
identity_format = direct
```

No per-user `pg_ident.conf` entry is then needed.

Direct mode does **not** enable PostgreSQL's `delegate_ident_mapping`. The
validator establishes the identity, while PostgreSQL still performs the exact
identity-to-requested-role check.

## Issuer-qualified format

With `identity_format = issuer_qualified`, the validator returns:

```text
v1.<base64url(issuer)>.<base64url(selected claim)>
```

This prevents identities from different issuers from colliding and provides a
reversible, stable external identity. It is useful when PostgreSQL administrators
want explicit local control over role assignment.

Because the encoded identity will normally differ from the requested role,
this format is normally used with a PostgreSQL usermap:

```conf
# pg_hba.conf
hostssl all all all oauth issuer="https://example.us.auth0.com/" \
    scope="connect:postgres" map=oauthmap

# pg_ident.conf
oauthmap  v1.<encoded-issuer>.<encoded-claim>  app_reader
```

## When PostgreSQL uses `pg_ident.conf`

The HBA rule decides this; the validator does not inspect `pg_ident.conf`.

- If the matching `pg_hba.conf` rule contains `map=oauthmap`, PostgreSQL looks
  up the validator's authenticated identity in that named map.
- If the HBA rule has no `map=`, PostgreSQL requires the authenticated identity
  to exactly equal the role requested by the client.
- If `delegate_ident_mapping=1` is configured, PostgreSQL skips both checks and
  makes the validator responsible for authorizing the requested role. The
  delegated roles mode below defines how the validator does that.

Changes to `pg_hba.conf` and `pg_ident.conf` require a PostgreSQL configuration
reload, not a server restart.

## Proposed defaults and guidance

The first release should support both formats rather than force one deployment
model:

- `direct` is the practical mode when the IdP provides a claim whose value is
  intentionally managed as a PostgreSQL role name.
- `issuer_qualified` is the conservative mode when PostgreSQL owns the
  identity-to-role authorization policy.
- `sub` remains a valid claim in either format. Direct `sub` matching is useful
  when operators deliberately create roles named after provider subjects.
- Provider profiles should document which claims are stable, mutable,
  user-controlled, or suitable for direct role matching.

The default format is still an open product decision. Whichever default is
chosen must be explicit in generated configuration and documentation.

## Security requirements for direct mode

Direct matching is safe only when the configured issuer controls the selected
claim appropriately. The implementation and documentation must therefore:

- bind validation to the exact administrator-configured issuer;
- reject missing, empty, non-string, oversized, or invalidly encoded claims;
- reject values that cannot be represented as PostgreSQL role names;
- warn against claims users may freely edit to names such as `postgres`;
- document claim mutability and reassignment behavior per provider profile;
- test that a token for one identity cannot connect as a different requested
  role;
- never create PostgreSQL roles automatically.

Direct mode intentionally allows the same role name to be used with several
trusted issuers when separate HBA rules authorize those issuers. Operators who
need globally distinct external identities should use `issuer_qualified`.

## Delegated roles mode

Both PostgreSQL 18 and 19 support `delegate_ident_mapping=1`. This proposal
therefore also supports an opt-in mode in which a token lists several roles the
identity may assume:

```json
{
  "sub": "google-oauth2|108329268577383920408",
  "roles": ["app_reader", "reporting"]
}
```

Proposed settings:

```conf
authorization_mode = claim_roles
roles_claim = roles
```

The client still chooses a PostgreSQL role when connecting. The validator
authorizes the connection only if the requested role is an exact,
case-sensitive member of the configured claim. `sub` or the configured
`identity_claim` remains the authenticated identity used for auditing; the
roles claim is authorization data, not the user's identity.

For example, a request for `reporting` succeeds with the token above, while a
request for `postgres` fails.

The HBA rule must explicitly enable delegation and should use its `USER` field
as a local boundary around the database roles reachable through that rule:

```conf
hostssl all app_reader,reporting all oauth \
    issuer="https://idp.example/" scope="connect:postgres" \
    delegate_ident_mapping=1
```

This gives two independent checks:

1. PostgreSQL selects the rule only for an administrator-approved requested
   role.
2. The validator requires that exact role in the validated token.

The HBA rule must not combine `delegate_ident_mapping=1` with `map=`.

### Delegated-role requirements

- Delegation must be explicitly enabled in both the HBA rule and validator
  policy. An accidental HBA change must fail closed.
- `roles_claim` must be configurable because providers use different top-level
  or nested claim names.
- The selected value must be a bounded array of unique, non-empty strings.
- Matching must be exact and case-sensitive; implicit case conversion, prefix
  removal, regular expressions, and provider-group-to-role conversion are not
  allowed in the initial implementation.
- The requested role must be present in the token even if it passed the HBA
  `USER` filter.
- Documentation must require explicit HBA allowlisting for delegated roles and
  must warn against `all`, superuser roles, and broad `+group` matches.
- The validator intentionally does not maintain another role allowlist,
  privileged-role denylist, or catalog-based privilege classifier. A privileged
  role is reachable when both HBA and the validated token explicitly permit it.
- Missing, malformed, oversized, or duplicate role claims must deny access.
- Tests must cover requests for absent roles and administrative roles, malformed
  arrays, multiple issuers, and configuration mismatches.
- The validator must never create roles or grant PostgreSQL memberships.

Provider profiles may recommend `roles`, a URI-namespaced Auth0 claim, or a
provider-specific claim path. Supporting nested claim paths requires a defined,
unambiguous path syntax and is a separate implementation detail.

PostgreSQL 18 and 19 use the same authorization behavior. PostgreSQL 19 can
select validator-specific named policy per HBA rule; PostgreSQL 18 must obtain
equivalent validator policy from its supported server-level configuration.

## Examples

| Desired policy | Claim | Format | `map=` needed? |
| --- | --- | --- | --- |
| Provider subject is the PostgreSQL role | `sub` | `direct` | No |
| IdP manages a dedicated database username | `postgres_username` | `direct` | No |
| PostgreSQL maps every external person locally | `sub` | `issuer_qualified` | Normally yes |
| One token may assume several explicit roles | `roles` or configured equivalent | delegated roles | No |
