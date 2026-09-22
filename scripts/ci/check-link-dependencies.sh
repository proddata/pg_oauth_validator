#!/bin/sh

set -eu

# Validates the link-time dependency configuration before a build starts.
#
# For every dependency this checks that pkg-config can see it and that it meets
# the reviewed minimum version. For a dependency selected for static linking it
# additionally checks that the archive path resolved, exists, and can actually
# be linked into a shared object.
#
# Failing here, with the dependency and the remedy named, is the point: the
# alternatives are a bare "/libjwt.a" from an unresolved pkg-config libdir, an
# opaque linker relocation error, or a compile against an API the installed
# library does not have.

usage()
{
	echo "usage: $0 CC [NAME MODE MIN_VERSION ARCHIVE]..." >&2
	exit 2
}

test "$#" -ge 5 || usage

cc=$1
shift

probe_dir=

# A static archive built without -fPIC links into executables but not into a
# shared object. Debian's libjansson-dev ships such an archive, and without this
# probe the only symptom is an opaque relocation error from the module link.
# Probe with the real compiler and linker instead of inspecting relocation
# types, which are architecture-specific.
probe_pic_archive()
{
	archive=$1

	case "$(uname -s)" in
		Linux) ;;
		*) return 0 ;;
	esac

	if test -z "$probe_dir"; then
		probe_dir=$(mktemp -d "${TMPDIR:-/tmp}/pg-oauth-archive.XXXXXX")
		trap 'rm -rf "$probe_dir"' EXIT HUP INT TERM
	fi

	if ! "$cc" -shared -fPIC -o "$probe_dir/probe.so" \
		-Wl,--whole-archive "$archive" -Wl,--no-whole-archive \
		-Wl,--unresolved-symbols=ignore-all >"$probe_dir/probe.log" 2>&1; then
		echo "error: $archive cannot be linked into a shared object;" \
			"rebuild it as a PIC static archive" >&2
		cat "$probe_dir/probe.log" >&2
		exit 1
	fi
}

while test "$#" -gt 0; do
	test "$#" -ge 4 || usage
	name=$1
	mode=$2
	min_version=$3
	archive=$4
	shift 4

	# LIBJWT_LINK_MODE, JANSSON_LINK_MODE: the switch a packager actually sets.
	mode_variable=$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')_LINK_MODE

	case "$mode" in
		static|shared) ;;
		*)
			echo "error: $name link mode must be static or shared" \
				"(found: $mode)" >&2
			exit 2
			;;
	esac

	if ! pkg-config --exists "$name" 2>/dev/null; then
		echo "error: $name is not visible to pkg-config." \
			"Install its development package, or set PKG_CONFIG_PATH to" \
			"the prefix holding $name.pc." >&2
		exit 1
	fi

	if ! pkg-config --atleast-version="$min_version" "$name"; then
		echo "error: $name $min_version or later is required" \
			"(found: $(pkg-config --modversion "$name"))." \
			"See docs/dependencies.md for the API that sets this floor." >&2
		exit 1
	fi

	test "$mode" = static || continue

	if test -z "$archive"; then
		echo "error: $name.pc declares no libdir, so its static archive path" \
			"cannot be resolved. Point PKG_CONFIG_PATH at a prefix whose" \
			"$name.pc declares libdir, or build with ${mode_variable}=shared" \
			"to link the deployment environment's shared library." >&2
		exit 1
	fi

	if ! test -f "$archive"; then
		echo "error: $name was selected for static linking but $archive does" \
			"not exist. Distribution development packages do not always" \
			"ship a static archive; build the reviewed archive, or build" \
			"with ${mode_variable}=shared to link the deployment" \
			"environment's shared library." >&2
		exit 1
	fi

	probe_pic_archive "$archive"
done
