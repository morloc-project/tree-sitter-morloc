#!/bin/sh
# Parse every .loc file under the given directories and report any that
# produce an ERROR or MISSING node.
#
# The morloc compiler's golden tests are the reference corpus: the grammar is
# a transcription of the compiler's parser, so every file the compiler builds
# must parse here.
#
#   test/parse-corpus.sh [DIR ...]
#
# With no argument it looks for the compiler checkout beside this one.

set -e

cd "$(dirname "$0")/.."

# Prefer a locally installed CLI so the script works without a global install.
TS=tree-sitter
if [ -x node_modules/.bin/tree-sitter ]; then
    TS=node_modules/.bin/tree-sitter
fi

if [ $# -eq 0 ]; then
    set -- ../../compiler/morloc/test-suite/golden-tests
fi

for dir in "$@"; do
    if [ ! -d "$dir" ]; then
        echo "no such directory: $dir" >&2
        exit 1
    fi
done

list=$(mktemp)
trap 'rm -f "$list"' EXIT
find "$@" -name '*.loc' | sort > "$list"

echo "parsing $(wc -l < "$list") files"
"$TS" parse --quiet --stat --paths "$list"
