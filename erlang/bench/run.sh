#!/bin/sh
# Time the benchmarks on MLWSVM, and with Erlang/OTP if it is installed
# (whose time includes starting the BEAM, about 0.2 s).
#   bench/run.sh [name...]
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -z "$MLWSVM" ] && ! command -v mlwsvm >/dev/null 2>&1; then
    MLWSVM=$(CDPATH= cd -- "$here/../../.." && pwd)/mlws-vm/mlwsvm
fi
. "$here/../../tools/find-vm.sh"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/erlbench.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
list="fib ring binarytrees nbody sorting"
secs() { /usr/bin/time -p "$@" 2>&1 >/dev/null | awk '/^real/ { print $2 }'; }
cd "$here/.." || exit 1
printf '%-14s %10s %10s\n' benchmark mlwsvm erl
for name in $list; do
    if [ $# -gt 0 ]; then case " $* " in *" $name "*) ;; *) continue ;; esac; fi
    ./mlws-vm-erlc -o "$tmp/$name.masm" "bench/$name.erl" || continue
    t1=$(secs "$MLWSVM" run --max-frames 50000000 "$tmp/$name.masm")
    t2=-
    if command -v erlc >/dev/null; then
        erlc -o "$tmp" "bench/$name.erl" && t2=$(secs erl -noshell -pa "$tmp" -eval "$name:main(), init:stop().")
    fi
    printf '%-14s %10s %10s\n' "$name" "$t1" "$t2"
done
