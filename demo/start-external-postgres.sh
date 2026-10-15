#!/bin/sh
set -eu

python3 /demo/render-profile.py /demo/profile.yml /demo/runtime
cp /demo/runtime/init.sql /docker-entrypoint-initdb.d/10-profile.sql
exec docker-entrypoint.sh postgres -c config_file=/demo/runtime/postgresql.conf
