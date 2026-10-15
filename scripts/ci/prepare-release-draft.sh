#!/bin/sh
set -eu

if test "$#" -ne 3; then
	echo "usage: $0 RELEASE_VERSION SOURCE_REVISION ARTIFACT_DIR" >&2
	exit 2
fi

version=$1
revision=$2
artifact_dir=$3

printf '%s\n' "$version" |
	LC_ALL=C grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$' || {
	echo "error: invalid release version" >&2
	exit 2
}
printf '%s\n' "$revision" |
	LC_ALL=C grep -Eq '^[0-9a-f]{40}([0-9a-f]{24})?$' || {
	echo "error: invalid source revision" >&2
	exit 2
}

test -d "$artifact_dir" && test ! -e "$artifact_dir/publish" || {
	echo "error: artifact directory is absent or already prepared" >&2
	exit 1
}

publish_dir=$artifact_dir/publish
mkdir "$publish_dir"

for major in 18 19; do
	input=$artifact_dir/pg$major
	test -d "$input" || {
		echo "error: missing PostgreSQL $major artifact directory" >&2
		exit 1
	}
	set -- "$input"/pg_oauth_validator-"$version"-pg"$major"*.tar.gz
	test "$#" -eq 1 && test -f "$1" || {
		echo "error: expected exactly one PostgreSQL $major archive" >&2
		exit 1
	}
	archive=$1
	test -f "$archive.sha256" && test -f "$archive.manifest" &&
		test -f "$input/RELEASE-EVIDENCE.md" || {
		echo "error: PostgreSQL $major release evidence is incomplete" >&2
		exit 1
	}
	test "$(find "$input" -mindepth 1 -maxdepth 1 -type f | wc -l)" -eq 4 &&
		test "$(find "$input" -mindepth 1 -maxdepth 1 | wc -l)" -eq 4 || {
		echo "error: unexpected PostgreSQL $major artifact" >&2
		exit 1
	}
	digest=$(sha256sum "$archive" | awk '{print $1}')
	test "$(cat "$archive.sha256")" = "$digest  $(basename "$archive")" || {
		echo "error: PostgreSQL $major checksum mismatch" >&2
		exit 1
	}
	(cd "$input" && sha256sum --check "$(basename "$archive").sha256")
	manifest=$archive.manifest
	for field in "release-version: $version" \
		"source-revision: $revision" \
		"postgresql-major: $major" \
		"architecture: x86_64"; do
		test "$(grep -Fxc "$field" "$manifest")" -eq 1 || {
			echo "error: PostgreSQL $major manifest field mismatch" >&2
			exit 1
		}
	done
	member=$(tar -tf "$archive" | sed -n '\|/BUILD-MANIFEST$|p')
	test "$(printf '%s\n' "$member" | wc -l)" -eq 1 &&
		test -n "$member" || {
		echo "error: PostgreSQL $major archive has no unique manifest" >&2
		exit 1
	}
	tar -xOf "$archive" "$member" | cmp - "$manifest"
	cp "$archive" "$archive.sha256" "$manifest" "$publish_dir/"
	cp "$input/RELEASE-EVIDENCE.md" \
		"$publish_dir/RELEASE-EVIDENCE-pg$major.md"
done

(cd "$publish_dir" &&
	find . -maxdepth 1 -type f ! -name SHA256SUMS -print | LC_ALL=C sort |
	while IFS= read -r file; do
		sha256sum "${file#./}"
	done >SHA256SUMS &&
	sha256sum --check SHA256SUMS)
