#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

# Keep this check independent of optional developer tools. Compiler formatting
# belongs in a checked-in style configuration before it becomes a merge gate.
git diff --check
if git rev-parse --verify HEAD >/dev/null 2>&1; then
    git log -1 --check --pretty=format:
fi

find scripts -type f -name '*.sh' -exec sh -n {} +

python_cache=$(mktemp -d "${TMPDIR:-/tmp}/pg-oauth-python.XXXXXX")
trap 'rm -rf "$python_cache"' EXIT HUP INT TERM
PYTHONPYCACHEPREFIX="$python_cache" python3 -m compileall -q tests

if find . -type f \( -name '*.orig' -o -name '*.rej' -o -name '*~' \) \
    -not -path './.git/*' | grep -q .; then
    echo "error: source tree contains editor or patch artifacts" >&2
    exit 1
fi
