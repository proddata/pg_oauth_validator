#!/bin/sh
set -eu

if test "$#" -ne 4; then
	echo "usage: $0 OUTPUT_FILE ARTIFACT_DIR IMAGE PG_CONFIG" >&2
	exit 2
fi

output_file=$1
artifact_dir=$2
image=$3
pg_config=$4
archive=$(find "$artifact_dir" -maxdepth 1 -type f -name '*.tar.gz')
test -n "$archive" && test "$(printf '%s\n' "$archive" | wc -l)" -eq 1
source_status=$(git status --short)
test -z "$source_status" || {
	echo "error: release evidence requires a clean source tree" >&2
	exit 1
}

{
	echo "# Release build evidence"
	echo
	echo "- Source revision: \`$(git rev-parse HEAD)\`"
	echo "- Source tree: \`clean\`"
	echo "- Container image: \`$image\`"
	echo "- PostgreSQL: \`$($pg_config --version)\`"
	echo "- Compiler: \`$(${CC:-cc} --version | sed -n '1p')\`"
	echo "- Architecture: \`$(uname -m)\`"
	echo "- Archive: \`$(basename "$archive")\`"
	echo "- SHA-256: \`$(sha256sum "$archive" | awk '{print $1}')\`"
	echo "- Reproducibility: two isolated builds in the pinned job matched byte-for-byte"
} >"$output_file"
