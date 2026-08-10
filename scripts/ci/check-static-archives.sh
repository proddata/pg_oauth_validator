#!/bin/sh

set -eu

if test "$#" -lt 2; then
	echo "usage: $0 CC ARCHIVE..." >&2
	exit 2
fi

cc=$1
shift

for archive in "$@"; do
	test -f "$archive" || {
		echo "error: PIC static archive is required: $archive" >&2
		exit 1
	}
done

# Existence is not enough: a static archive built without -fPIC links into
# executables but not into a shared object. Debian's libjansson-dev ships such
# an archive, and without this probe the only symptom is an opaque relocation
# error from the module link. Probe with the real compiler and linker instead of
# inspecting relocation types, which are architecture-specific.
case "$(uname -s)" in
	Linux) ;;
	*) exit 0 ;;
esac

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/pg-oauth-archive.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

for archive in "$@"; do
	if ! "$cc" -shared -fPIC -o "$work_dir/probe.so" \
		-Wl,--whole-archive "$archive" -Wl,--no-whole-archive \
		-Wl,--unresolved-symbols=ignore-all >"$work_dir/probe.log" 2>&1; then
		echo "error: $archive cannot be linked into a shared object;" \
			"rebuild it as a PIC static archive" >&2
		cat "$work_dir/probe.log" >&2
		exit 1
	fi
done
