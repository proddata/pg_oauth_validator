#!/bin/sh
set -eu

if test "$#" -lt 2 || test "$#" -gt 4; then
    echo "usage: $0 STAGE_ROOT PG_CONFIG [JANSSON_LINK_MODE] [LIBJWT_LINK_MODE]" >&2
    exit 2
fi

stage=$1
pg_config=$2
# Default to the release contract, so a caller that omits the modes still gets
# the strict static check.
jansson_link_mode=${3:-static}
libjwt_link_mode=${4:-static}
pkglibdir=$($pg_config --pkglibdir)
docdir=$($pg_config --docdir)
library="$stage$pkglibdir/pg_oauth_validator.so"
staged_docdir="$stage$docdir/contrib"

test -f "$library"
test -f "$staged_docdir/README.md"
test -f "$staged_docdir/operations.md"
test -f "$staged_docdir/PROVIDER-COMPATIBILITY.md"
test -f "$staged_docdir/LICENSE"
test -f "$staged_docdir/THIRD-PARTY-NOTICES.md"

if find "$stage" -type f -perm /022 | grep -q .; then
    echo "error: staged package contains group/world-writable files" >&2
    exit 1
fi

# Verify the linkage the build actually selected, in both directions. A static
# selection must not leave a runtime dependency, and a shared selection must
# not have silently linked an archive instead -- which is how a packager would
# otherwise ship a binary whose embedded copy nobody is patching.
check_linkage()
{
    name=$1
    pattern=$2
    mode=$3

    if ldd "$library" | grep -Eq "$pattern"; then
        if test "$mode" != shared; then
            echo "error: $name must remain statically embedded, but the" \
                "staged library has a runtime dependency on it" >&2
            exit 1
        fi
    else
        if test "$mode" != static; then
            echo "error: $name was selected for shared linking but the" \
                "staged library has no runtime dependency on it; the build" \
                "linked an archive instead" >&2
            exit 1
        fi
    fi
}

check_linkage Jansson 'libjansson' "$jansson_link_mode"
check_linkage libjwt 'libjwt' "$libjwt_link_mode"

nm -D "$library" | grep -q '_PG_oauth_validator_module_init'

file_count=$(find "$stage" -type f | wc -l | tr -d ' ')
bitcode_dir="$stage$pkglibdir/bitcode/pg_oauth_validator"
if test -d "$bitcode_dir"; then
    test -f "$stage$pkglibdir/bitcode/pg_oauth_validator.index.bc"
    bitcode_count=$(find "$bitcode_dir" -type f -name '*.bc' | wc -l | tr -d ' ')
    test "$bitcode_count" -eq 18
    expected_count=25
else
    expected_count=6
fi

if test "$file_count" -ne "$expected_count"; then
    echo "error: unexpected staged package manifest ($file_count files)" >&2
    find "$stage" -type f -print >&2
    exit 1
fi
