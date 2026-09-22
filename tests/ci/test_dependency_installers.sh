#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

assert_rejects_missing_archive()
{
	script=$1
	variable=$2
	output=$work_dir/missing.out

	if env "$variable=$work_dir/missing.tar.gz" \
		sh "$repo_root/$script" >"$output" 2>&1; then
		echo "error: $script accepted a missing source archive" >&2
		exit 1
	fi
	grep -q 'is not a readable file' "$output"
}

assert_rejects_wrong_digest_without_network()
{
	script=$1
	variable=$2
	archive=$work_dir/invalid.tar.gz
	marker=$work_dir/curl-called
	bin_dir=$work_dir/bin

	printf '%s\n' 'not the reviewed source archive' >"$archive"
	mkdir -p "$bin_dir"
	{
		echo '#!/bin/sh'
		printf 'touch %s\n' "$marker"
		echo 'exit 99'
	} >"$bin_dir/curl"
	chmod +x "$bin_dir/curl"

	if env PATH="$bin_dir:$PATH" "$variable=$archive" \
		sh "$repo_root/$script" >/dev/null 2>&1; then
		echo "error: $script accepted an archive with the wrong digest" >&2
		exit 1
	fi
	test ! -e "$marker" || {
		echo "error: $script used the network with a supplied archive" >&2
		exit 1
	}
}

assert_rejects_missing_archive scripts/ci/install-jansson.sh \
	JANSSON_SOURCE_ARCHIVE
assert_rejects_wrong_digest_without_network scripts/ci/install-jansson.sh \
	JANSSON_SOURCE_ARCHIVE
assert_rejects_missing_archive scripts/ci/install-libjwt.sh \
	LIBJWT_SOURCE_ARCHIVE
assert_rejects_wrong_digest_without_network scripts/ci/install-libjwt.sh \
	LIBJWT_SOURCE_ARCHIVE
