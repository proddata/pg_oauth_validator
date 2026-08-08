#!/bin/sh
set -eu

if test "$#" -ne 3; then
	echo "usage: $0 FIRST_OUTPUT SECOND_OUTPUT EXPECTED_PG_MAJOR" >&2
	exit 2
fi

first_output=$1
second_output=$2
expected_major=$3

case "$expected_major" in
	18|19) ;;
	*)
		echo "error: expected PostgreSQL major must be 18 or 19" >&2
		exit 2
		;;
esac

find_one()
{
	directory=$1
	suffix=$2
	set -- "$directory"/*"$suffix"
	test "$#" -eq 1 && test -f "$1" || {
		echo "error: expected exactly one *$suffix in $directory" >&2
		exit 1
	}
	printf '%s\n' "$1"
}

first_archive=$(find_one "$first_output" .tar.gz)
second_archive=$(find_one "$second_output" .tar.gz)
first_manifest=$(find_one "$first_output" .tar.gz.manifest)
second_manifest=$(find_one "$second_output" .tar.gz.manifest)

(cd "$first_output" && sha256sum --check "$(basename "$first_archive").sha256")
(cd "$second_output" && sha256sum --check "$(basename "$second_archive").sha256")

cmp "$first_archive" "$second_archive"
cmp "$first_manifest" "$second_manifest"

manifest_major=$(sed -n 's/^postgresql-major: //p' "$first_manifest")
test "$manifest_major" = "$expected_major" || {
	echo "error: package is for PostgreSQL $manifest_major, not $expected_major" >&2
	exit 1
}

archive_manifest=$(mktemp)
trap 'rm -f "$archive_manifest"' EXIT HUP INT TERM
manifest_member=$(tar -tf "$first_archive" | sed -n '\|/BUILD-MANIFEST$|p')
test -n "$manifest_member" &&
	test "$(printf '%s\n' "$manifest_member" | wc -l)" -eq 1 || {
	echo "error: archive must contain exactly one BUILD-MANIFEST" >&2
	exit 1
}
tar -xOf "$first_archive" "$manifest_member" >"$archive_manifest"
cmp "$first_manifest" "$archive_manifest"

echo "release artifacts are reproducible and target PostgreSQL $expected_major"
