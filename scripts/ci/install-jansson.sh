#!/bin/sh

set -eu

# Debian's libjansson-dev ships a non-PIC static archive, which cannot be linked
# into a shared object. Build the reviewed release from an immutable source
# digest with position-independent code so the module can keep Jansson embedded.
JANSSON_COMMIT=dbb5fb3636e155fccfce4cd215de752779bd6971
JANSSON_SHA256=65084e4e43de9840d66a0604c8d9d9c499b2fc0db52c05730e3b6ac3c11ed66f
JANSSON_URL="https://codeload.github.com/akheron/jansson/tar.gz/${JANSSON_COMMIT}"
INSTALL_PREFIX=${JANSSON_INSTALL_PREFIX:-/usr/local}

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/pg-oauth-jansson.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

archive="$work_dir/jansson.tar.gz"
source_dir="$work_dir/source"
build_dir="$work_dir/build"

curl --fail --location --proto '=https' --tlsv1.2 --output "$archive" \
	"$JANSSON_URL"
printf '%s  %s\n' "$JANSSON_SHA256" "$archive" | sha256sum --check --status

mkdir -p "$source_dir" "$build_dir"
tar --extract --gzip --file "$archive" --directory "$source_dir" \
	--strip-components=1

cmake -S "$source_dir" -B "$build_dir" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_C_FLAGS="${CFLAGS:-}" \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DBUILD_SHARED_LIBS=OFF \
	-DJANSSON_BUILD_SHARED_LIBS=OFF \
	-DJANSSON_BUILD_DOCS=OFF \
	-DJANSSON_EXAMPLES=OFF \
	-DJANSSON_WITHOUT_TESTS=ON \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
cmake --build "$build_dir" --parallel 2
cmake --install "$build_dir"
install -d "$INSTALL_PREFIX/share/doc/jansson"
install -m 0644 "$source_dir/LICENSE" \
	"$INSTALL_PREFIX/share/doc/jansson/LICENSE"
