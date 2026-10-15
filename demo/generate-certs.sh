#!/bin/sh
set -eu

umask 077
mkdir -p /certs
if test -s /certs/ca.crt && test -s /certs/postgres.key && test -s /certs/keycloak.key; then
    exit 0
fi

openssl req -x509 -newkey rsa:3072 -nodes -days 7 \
    -subj /CN=pg-oauth-demo-ca \
    -keyout /certs/ca.key -out /certs/ca.crt >/dev/null 2>&1

for name in postgres keycloak; do
    case "$name" in
        postgres) hostname=postgres ;;
        keycloak) hostname=localhost ;;
    esac
    openssl req -new -newkey rsa:2048 -nodes \
        -subj "/CN=$hostname" \
        -addext "subjectAltName=DNS:$hostname,DNS:localhost" \
        -keyout "/certs/$name.key" -out "/certs/$name.csr" >/dev/null 2>&1
    openssl x509 -req -days 7 -in "/certs/$name.csr" \
        -CA /certs/ca.crt -CAkey /certs/ca.key -CAcreateserial \
        -copy_extensions copy \
        -out "/certs/$name.crt" >/dev/null 2>&1
    rm -f "/certs/$name.csr"
done

rm -f /certs/ca.key /certs/ca.srl
chown postgres:postgres /certs/postgres.key
chown 1000:0 /certs/keycloak.key
chmod 0600 /certs/postgres.key /certs/keycloak.key
chmod 0644 /certs/*.crt
