#!/bin/sh

set -eu

LIBJWT_COMMIT=602118d99d46ca5df71bda60d3df642135417f29
LIBJWT_SHA256=47e3d5d00fd60141dbacbcb9a0c2e1b277740364ac1d15b50728dccb7afdf23c
LIBJWT_URL="https://codeload.github.com/benmcollins/libjwt/tar.gz/${LIBJWT_COMMIT}"
INSTALL_PREFIX=${LIBJWT_INSTALL_PREFIX:-/usr/local}

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/pg-oauth-libjwt.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

archive="$work_dir/libjwt.tar.gz"
source_dir="$work_dir/source"
build_dir="$work_dir/build"
case "${CC:-cc}" in
	*clang*) dependency_warning_flags= ;;
	*) dependency_warning_flags=-Wno-stringop-truncation ;;
esac
dependency_cflags="$dependency_warning_flags ${CFLAGS:-}"

curl --fail --location --proto '=https' --tlsv1.2 --output "$archive" \
	"$LIBJWT_URL"
printf '%s  %s\n' "$LIBJWT_SHA256" "$archive" | sha256sum --check --status

mkdir -p "$source_dir" "$build_dir"
tar --extract --gzip --file "$archive" --directory "$source_dir" \
	--strip-components=1

cmake -S "$source_dir" -B "$build_dir" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_C_FLAGS="$dependency_cflags" \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
	-DEXCLUDE_DEPRECATED=ON \
	-DWITH_GNUTLS=OFF \
	-DWITH_JSON_C=OFF \
	-DWITH_LIBCURL=OFF \
	-DWITH_MBEDTLS=OFF \
	-DWITH_TESTS=OFF
cmake --build "$build_dir" --parallel 2
cmake --install "$build_dir"

if command -v ldconfig >/dev/null 2>&1 && test "$INSTALL_PREFIX" = /usr/local; then
	ldconfig
fi
