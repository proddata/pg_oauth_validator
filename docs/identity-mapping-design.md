# Identity and role mapping

The current identity and role contract is specified in [FEATURES.md](../FEATURES.md)
and explained with configuration examples in [configuration.md](configuration.md).
Every accepted token has `iss` and `sub`; the selected `identity_claim` is
returned directly as `authn_id`. PostgreSQL performs exact matching or a
`pg_ident.conf` usermap unless a configured `role_claim` and the matched HBA
rule together enable delegated role authorization.
