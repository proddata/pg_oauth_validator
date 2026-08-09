# OAuth validator playground

This playground creates named, disposable PostgreSQL configurations for trying
the validator with real identity providers. Auth0 is the first supported
provider. It uses the custom Node.js OAuth client because stock libpq currently
constructs Auth0's discovery URL incorrectly; `psql` remains selectable for
demonstrating and tracking that interoperability behavior.

The playground does not bundle an identity provider. Provider-specific values
and rendered PostgreSQL files live only in the ignored `.generated/` directory.

## Create a profile

From this directory:

```sh
./auth0-playground.py create roles-test
```

Numbered menus select:

- direct identity, issuer-qualified usermap, or delegated claim roles;
- the PostgreSQL role requested by the clients;
- Node.js, psql, or both clients.

The CLI then asks for the Auth0 tenant and API values and creates a reviewable
profile under `.generated/auth0/roles-test/`. Every profile contains its own
`profile.json`, `postgresql.conf`, HBA files, usermap, `init.sql`, and client
configuration.

## Run a profile

```sh
./auth0-playground.py start roles-test
./auth0-playground.py start roles-test --pg 19
```

The saved client selection is used by default. Override it for one run with:

```sh
./auth0-playground.py start roles-test --client node
```

Containers and networks are stopped after the test, including on failure, so
the host port is released. The database volume is retained. Use
`--keep-running` for manual inspection or delete the retained volume with:

```sh
./auth0-playground.py reset roles-test
```

List and inspect profiles with:

```sh
./auth0-playground.py list
./auth0-playground.py show roles-test
```

See [`providers/auth0/README.md`](providers/auth0/README.md) for Auth0 setup,
claim examples, the current psql limitation, and security notes.

Run the offline profile-generator tests with:

```sh
./tests/auth0-playground.sh
```
