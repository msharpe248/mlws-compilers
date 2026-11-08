#!/bin/sh
# The --modules tests: each directory holds a program, main.erl, with the
# modules it calls next to it, the new versions of some in v2/, and what it
# prints, main.out (and, if it needs a later MLWSVM than 0.18.1, which, in
# needs). The program is compiled with --modules to a directory,
# v2's modules to its v2/, and run with that directory as its argument.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
erlc=$here/../../mlws-vm-erlc
if [ -z "$MLWSVM" ] && ! command -v mlwsvm >/dev/null 2>&1; then
    MLWSVM=$(CDPATH= cd -- "$here/../../.." && pwd)/../mlws-vm/mlwsvm
fi
. "$here/../../../tools/find-vm.sh"
if ! "$MLWSVM" version |
       awk '{ split($2, v, "."); exit !(v[1] * 1000000 + v[2] * 1000 + v[3] >= 18001) }'; then
    echo "modules: skipped (they need MLWSVM 0.18.1)"
    exit 0
fi
pass=0 fail=0
ver=$("$MLWSVM" version | awk '{ split($2, v, "."); print v[1] * 1000000 + v[2] * 1000 + v[3] }')
for d in "$here"/*/; do
    # a test that needs a later MLWSVM says which, in its file needs
    if [ -f "$d/needs" ] &&
       [ "$ver" -lt "$(awk '{ split($1, v, "."); print v[1] * 1000000 + v[2] * 1000 + v[3] }' "$d/needs")" ]; then
        continue
    fi
    t=$(mktemp -d "${TMPDIR:-/tmp}/erlmod.XXXXXX") || exit 1
    mkdir "$t/v2"
    ok=1
    "$erlc" --modules "$t" "$d/main.erl" || ok=
    for f in "$d"/v2/*.erl; do
        [ -f "$f" ] && { "$erlc" --modules "$t/v2" "$f" || ok=; }
    done
    if [ -n "$ok" ] && "$MLWSVM" run --max-frames 50000000 "$t/main.masm" "$t" > "$t/out" 2>&1 &&
       cmp -s "$t/out" "$d/main.out"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL tests/modules/$(basename "$d")"
        [ -f "$t/out" ] && diff "$d/main.out" "$t/out" | head -10
    fi
    rm -rf "$t"
done
echo "modules: $pass passed, $fail failed"
[ $fail -eq 0 ]
