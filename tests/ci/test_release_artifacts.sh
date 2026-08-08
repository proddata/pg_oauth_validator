#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
checker=$repo_root/scripts/ci/check-release-artifacts.sh
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

make_artifact()
{
	output=$1
	major=$2
	root=$work_dir/root/pg_oauth_validator-0.1.0-pg$major
	rm -rf "$work_dir/root"
	mkdir -p "$output" "$root"
	{
		echo "format-version: 1"
		echo "postgresql-major: $major"
	} >"$root/BUILD-MANIFEST"
	tar -C "$work_dir/root" -cf - "$(basename "$root")" |
		gzip -n >"$output/package.tar.gz"
	sha256sum "$output/package.tar.gz" |
		sed 's|  .*/|  |' >"$output/package.tar.gz.sha256"
	cp "$root/BUILD-MANIFEST" "$output/package.tar.gz.manifest"
}

make_artifact "$work_dir/first" 18
cp -R "$work_dir/first" "$work_dir/second"
"$checker" "$work_dir/first" "$work_dir/second" 18

if "$checker" "$work_dir/first" "$work_dir/second" 19 >/dev/null 2>&1; then
	echo "error: wrong PostgreSQL major was accepted" >&2
	exit 1
fi

printf '\n' >>"$work_dir/second/package.tar.gz.manifest"
if "$checker" "$work_dir/first" "$work_dir/second" 18 >/dev/null 2>&1; then
	echo "error: non-reproducible manifests were accepted" >&2
	exit 1
fi
