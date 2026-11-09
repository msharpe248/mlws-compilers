#!/bin/sh
# Distribution: two MLWSVM nodes, a and b, on this machine, found through a
# `mlwsvm names` of their own (on a free port). b.erl and a.erl are compiled
# with --modules into one directory, so both run the same erlrt; b runs in
# the background and a's output must be a.out. Both run with the driver's
# --max-frames, for the terms 100000 deep.
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
erlc=$here/../../mlws-vm-erlc
if [ -z "$MLWSVM" ] && ! command -v mlwsvm >/dev/null 2>&1; then
    MLWSVM=$(CDPATH= cd -- "$here/../../.." && pwd)/../mlws-vm/mlwsvm
fi
. "$here/../../../tools/find-vm.sh"
t=$(mktemp -d "${TMPDIR:-/tmp}/erldist.XXXXXX") || exit 1
MLWSVM_NAMES_PORT=$(( 20000 + $$ % 20000 ))
export MLWSVM_NAMES_PORT
names= b= dog=
trap 'kill $names $b $dog 2>/dev/null; rm -rf "$t"' EXIT INT TERM
"$MLWSVM" names --port "$MLWSVM_NAMES_PORT" >"$t/names.log" 2>&1 &
names=$!
ok=1
# the big terms are smaller where every allocation collects or checks
size=1000000
[ -n "$MLWSVM_GC_STRESS$MLWSVM_GC_VERIFY" ] && size=${DIST_STRESS_SIZE:-10000}
"$erlc" --modules "$t" "$here/b.erl" && "$erlc" --modules "$t" "$here/a.erl" || ok=
if [ -n "$ok" ]; then
    "$MLWSVM" run --max-frames 50000000 "$t/b.masm" >"$t/b.log" 2>&1 &
    b=$!
    # neither may wait for ever on the other: both go after ${DIST_TIMEOUT:-300}s
    ( sleep "${DIST_TIMEOUT:-300}"; kill $b 2>/dev/null; pkill -f "$t/a.masm" ) >/dev/null 2>&1 &
    dog=$!
    "$MLWSVM" run --max-frames 50000000 "$t/a.masm" $size >"$t/a.log" 2>&1 && cmp -s "$t/a.log" "$here/a.out" || ok=
    wait $b 2>/dev/null
    kill $dog 2>/dev/null
fi
if [ -n "$ok" ]; then
    echo "dist: 1 passed, 0 failed"
else
    echo "FAIL tests/dist"
    [ -f "$t/a.log" ] && diff "$here/a.out" "$t/a.log" | head -20
    [ -s "$t/b.log" ] && { echo "b:"; head -10 "$t/b.log"; }
    echo "dist: 0 passed, 1 failed"
    exit 1
fi
