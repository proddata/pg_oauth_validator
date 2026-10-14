# Proposal: identity and PostgreSQL role configuration

Status: accepted pre-release design, implemented in the validator. `FEATURES.md`
is the authoritative specification; `configuration.md` is the operator guide.
No production deployment uses the superseded configuration.

## Goal

Keep two values distinct throughout OAuth connection authentication:

- **Authenticated identity:** the principal identified by the validated
  access token, not proof of who physically presented a bearer token. The
  default is the token's `sub`. The validator returns this as `authn_id`, which
  PostgreSQL exposes as `system_user` with an `oauth:` prefix and records in
  the connection authentication log when `log_connections` is enabled.
- **Requested PostgreSQL role:** the role supplied by the client to PostgreSQL.
  The validator receives it as the callback's `role` argument. The role is
  authorized by exact identity matching, a PostgreSQL usermap, or an explicit
  token role claim. It is not substituted for the authenticated identity.

The matched HBA rule must also allow the requested role. A validated token,
including a matching role claim, does not override the HBA role boundary.

## Supported modes

Token snippets below show only the claims relevant to identity and role
mapping; real accepted access tokens must also satisfy all required signature,
type, audience, expiry, and scope checks.

| Mode | Token inputs | HBA option | Final requested-role check | Returned `authn_id` |
| --- | --- | --- | --- | --- |
| Exact identity | `identity_claim` (default `sub`) | Neither `map` nor `delegate_ident_mapping` | PostgreSQL requires identity = requested role | Validated identity claim |
| PostgreSQL usermap | `identity_claim` (default `sub`) | `map=...` | PostgreSQL requires a matching `pg_ident.conf` identity → requested-role entry | Validated identity claim |
| Scalar token role | `identity_claim` (default `sub`) and a configured string `role_claim` | `delegate_ident_mapping=1`, without `map` | Validator requires role claim = requested role; HBA must allow the role | Validated identity claim |
| Array of token roles | `identity_claim` (default `sub`) and a configured string-array `role_claim` | `delegate_ident_mapping=1`, without `map` | Validator requires requested role as an exact array member; HBA must allow the role | Validated identity claim |

If the identity itself is also the PostgreSQL role name, use exact identity
mode. A separate `role_claim` naming the same claim is unnecessary. A usermap
can authorize a different role without delegation or a token role claim. A
different role **supplied by the token** requires delegation, because without
it PostgreSQL still checks the returned identity against the requested role or
usermap.

### Example: exact identity

```json
{ "iss": "https://idp.example/", "sub": "alice" }
```

The client requests PostgreSQL role `alice`. The validator returns `alice`;
PostgreSQL performs its normal exact-name check. No role claim is read.

### Example: PostgreSQL usermap

```json
{ "iss": "https://idp.example/", "sub": "alice" }
```

The client requests `app_reader`. The validator returns `alice`. An HBA rule
selects `map=oauthmap`, and `pg_ident.conf` contains:

```conf
oauthmap alice app_reader
```

PostgreSQL owns this mapping. The map uses `alice`, without the `oauth:` prefix.
The usermap may contain more than one permitted role for a subject, with each
pair explicit.

### Example: scalar token role

```json
{
  "iss": "https://idp.example/",
  "sub": "alice",
  "postgres_role": "app_reader"
}
```

The client requests `app_reader`. The validator validates `sub`, checks that
`postgres_role` is exactly `app_reader`, and returns `alice` as `authn_id`.
The HBA rule must enable `delegate_ident_mapping=1` and allow `app_reader` in
its `USER` field. PostgreSQL deliberately skips its identity-to-role check in
this mode, so the validator must finish the role check before approving login.
An IdP must control the role claim as database authorization policy; a client
must not be able to choose its value freely.

```conf
# pg_hba.conf (illustrative)
hostssl appdb app_reader 10.0.0.0/8 oauth \
    issuer="https://idp.example/" scope="connect:postgres" \
    validator=pg_oauth_validator delegate_ident_mapping=1
```

### Example: array of token roles

```json
{
  "iss": "https://idp.example/",
  "sub": "alice",
  "postgres_roles": ["app_reader", "reporting"]
}
```

The client may request either listed role, provided the HBA `USER` field also
allows it. The validator checks exact array membership and returns `alice` as
`authn_id`. The array must be nonempty, bounded, and contain distinct,
nonempty PostgreSQL role names. No string coercion or partial match is allowed.
This mode also requires `delegate_ident_mapping=1` and cannot use `map=`.

## Configuration shape

The implemented settings are:

```conf
# Always required after token validation; default shown.
pg_oauth_validator.identity_claim = 'sub'

# Set only for token-role authorization. No implicit role claim.
pg_oauth_validator.role_claim = 'postgres_role'
```

For the array example, set `role_claim='postgres_roles'` instead. The same
setting accepts either one role-name string or an array of role-name strings.

- An absent `role_claim` means PostgreSQL performs exact matching or the HBA
  selected usermap. It must not silently enable delegated authorization.
- A configured `role_claim` is valid only when the matched HBA rule sets
  `delegate_ident_mapping=1`. Conversely, delegation without a configured role
  claim fails closed. This preserves the current two-sided delegation guard
  while avoiding a separate `authorization_mode` switch.
- `map=` and `delegate_ident_mapping=1` cannot be combined.
- The role claim must be either one nonempty role-name string or a nonempty,
  bounded array of distinct role-name strings. In either shape, the requested
  role must match exactly. Every value must fit PostgreSQL's role-name length
  limit. Missing, wrong-type, empty, oversized, duplicate, or nonmatching
  values deny login. The validator never coerces a value or selects the first
  array element.
- Accepting both shapes is an explicit policy: a provider change from one
  string to an array can expand the roles available to a token holder. The
  trusted issuer controls that claim, and the HBA `USER` list remains an
  independent cap. Operators must review provider claim mappings accordingly.
- The validated identity claim must be a nonempty, bounded string. Its value is
  never replaced with the requested role merely to satisfy PostgreSQL's exact
  check.
- Every accepted token must contain valid `iss` and `sub`, even when
  `identity_claim` selects another claim. The selected claim must also be
  present and valid. This also applies when a custom identity claim is selected.
- There is no `identity_format` setting. The validator returns
  the selected identity claim directly. Issuer-qualified formatting is not
  part of the replacement contract.

## Audit behavior

With the default identity claim and a token having `sub=alice`, all four
modes return `authn_id=alice`. PostgreSQL's connection authentication line
records `identity="alice" method=oauth`; SQL `system_user` is `oauth:alice`.
The connection authorization line and `%u` log prefix identify the requested
PostgreSQL role. Standard pgAudit statement records do not include
`system_user`; correlation by PostgreSQL session ID requires complete,
ordered logs and must leave missing or conflicting evidence unattributed.
Connection pooling cannot identify a later logical user on a reused backend.

If `identity_claim` is changed, the logged identity changes too. The raw `sub`
is not also logged by the current callback. A requirement to *always* retain
`sub` for auditing would therefore either make `sub` the fixed identity claim
or need a separately reviewed logging mechanism.

## Implementation transition before first release

The replacement does not retain old settings or interpret them as aliases.
This project assumes no existing users or deployed configurations. Update
repository examples, tests, playgrounds, and local development configurations
manually. Stale validator settings must cause a clear configuration failure
rather than be ignored.

| Current setting or behavior | Replacement |
| --- | --- |
| `identity_claim` defaults to `sub` and may select a custom claim | Keep, but require valid `sub` independently |
| `identity_format='direct'` or `'issuer_qualified'` | Remove the setting; always return the selected identity claim directly |
| `authorization_mode='identity'` | Remove the setting; no `role_claim` means PostgreSQL exact matching or HBA `map=` |
| `authorization_mode='claim_roles'` and `roles_claim` | Remove both settings; use `role_claim` with HBA delegation for either a scalar or array |
| `identity_claim='postgres_role'` used to make a role name the recorded identity | Replace with `identity_claim='sub'` plus `role_claim='postgres_role'` and HBA delegation, or a PostgreSQL usermap |

Manual updates must remove `identity_format`, `authorization_mode`, and
`roles_claim`; set `role_claim` only for token-driven role authorization; and
review HBA delegation or usermap rules. There is no automatic translation or
compatibility mode. Before the first release, test both allowed and denied
roles in clean PostgreSQL 18 and 19 environments.

## Verification

The unit and PostgreSQL integration suites cover exact identity, usermaps,
scalar and array role claims, delegation mismatches, subject validation,
removed settings, and safe diagnostics. PostgreSQL 19 remains a preview target;
the local suite passed against the pinned Beta 4 installation.
