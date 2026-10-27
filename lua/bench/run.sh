#!/bin/sh
# Time the benchmarks on MLWSVM, and with the reference lua if it is installed.
#   bench/run.sh [name...]
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -z "$MLWSVM" ] && ! command -v mlwsvm >/dev/null 2>&1; then
    MLWSVM=$(CDPATH= cd -- "$here/../../.." && pwd)/mlws-vm/mlwsvm
fi
. "$here/../../tools/find-vm.sh"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luabench.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
# name, file, arguments
list="fib bench/fib.lua 32
loop bench/loop.lua 30000000
methods bench/methods.lua
strings bench/strings.lua
iterate bench/iterate.lua
binarytrees tests/programs/binarytrees.lua 15
nbody tests/programs/nbody.lua 500000
spectralnorm tests/programs/spectralnorm.lua 500
fannkuch tests/programs/fannkuch.lua 10"
secs() { /usr/bin/time -p "$@" 2>&1 >/dev/null | awk '/^real/ { print $2 }'; }
cd "$here/.." || exit 1
printf '%-14s %10s %10s\n' benchmark mlwsvm lua
echo "$list" | while read -r name file args; do
    if [ $# -gt 0 ]; then case " $* " in *" $name "*) ;; *) continue ;; esac; fi
    ./mlws-vm-luac -o "$tmp/$name.masm" "$file" || continue
    t1=$(secs "$MLWSVM" run "$tmp/$name.masm" $args)
    t2=-
    command -v lua >/dev/null && t2=$(secs lua "$file" $args)
    printf '%-14s %10s %10s\n' "$name" "$t1" "$t2"
done
