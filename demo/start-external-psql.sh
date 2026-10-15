#!/bin/sh
set -eu

python3 /demo/render-profile.py /demo/profile.yml /demo/runtime
if test -f /demo/runtime/provider-ca.crt; then
    cp /demo/runtime/provider-ca.crt /usr/local/share/ca-certificates/pg-oauth-provider.crt
    update-ca-certificates >/dev/null
fi
export PGSERVICEFILE=/demo/runtime/pg_service.conf
exec psql -X -v ON_ERROR_STOP=1 'service=external' "$@"
