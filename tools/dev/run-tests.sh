#!/bin/sh

set -eu

: "${PG_MAJOR:?PG_MAJOR is required}"
: "${PG_CONFIG_PATH:?PG_CONFIG_PATH is required}"

if test "$#" -eq 0; then
	echo "error: a make target is required" >&2
	exit 2
fi

# VPATH must not see compiler products left in the developer's working tree.
# Each `compose run` gets a fresh tmpfs mirror; the separate /build volume keeps
# the expensive, per-major compiler cache between runs.
cp -a /workspace/. /source/
make -C /source clean PG_CONFIG="$PG_CONFIG_PATH" >/dev/null

exec make -C /source "$@" \
	BUILD_ROOT=/build \
	"PG${PG_MAJOR}_CONFIG=$PG_CONFIG_PATH"
