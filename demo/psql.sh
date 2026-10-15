#!/bin/sh
set -eu

# libpq's built-in device flow uses libcurl's system trust store.
cp /certs/ca.crt /usr/local/share/ca-certificates/pg-oauth-demo.crt
update-ca-certificates >/dev/null

exec psql -X -v ON_ERROR_STOP=1 \
    "host=localhost dbname=demo user=app_reader sslmode=verify-full sslrootcert=/certs/ca.crt oauth_issuer=https://localhost:8443/realms/pg-oauth oauth_client_id=postgres-demo oauth_scope=connect:postgres" \
    "$@"
