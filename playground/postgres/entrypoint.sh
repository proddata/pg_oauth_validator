#!/bin/sh
set -eu

state_dir=/playground-state
certificate=$state_dir/postgres.crt
private_key=$state_dir/postgres.key

mkdir -p "$state_dir"
if test ! -s "$certificate" || test ! -s "$private_key"; then
	openssl req -x509 -newkey rsa:2048 -nodes -days 7 \
		-subj /CN=postgres \
		-addext subjectAltName=DNS:postgres,DNS:localhost,IP:127.0.0.1 \
		-keyout "$private_key" -out "$certificate" >/dev/null 2>&1
fi
chown postgres:postgres "$certificate" "$private_key"
chmod 0600 "$private_key"
chmod 0644 "$certificate"

exec docker-entrypoint.sh "$@"
