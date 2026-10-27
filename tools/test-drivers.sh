#!/bin/sh
# Check a compiler driver's ways of producing assembly (-S, -o, -k).
#   test-drivers.sh DRIVER PROGRAM EXPECTED_STDOUT
# PROGRAM is copied to a scratch directory; EXPECTED_STDOUT is what running
# it prints.
driver=$1 prog=$2 expect=$3
[ -x "$driver" ] && [ -f "$prog" ] && [ -f "$expect" ] || {
    echo "usage: test-drivers.sh DRIVER PROGRAM EXPECTED_STDOUT" >&2; exit 2; }
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
driver=$(CDPATH= cd -- "$(dirname -- "$driver")" && pwd)/$(basename -- "$driver")
tmp=$(mktemp -d "${TMPDIR:-/tmp}/drivers.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
name=$(basename -- "$prog")
cp "$prog" "$tmp/$name"
src="$tmp/$name" masm="$tmp/${name%.*}.masm"
. "$here/find-vm.sh"

passed=0 failed=0
check() { # description, then a command that must succeed
    desc=$1; shift
    if "$@" >"$tmp/out" 2>&1; then passed=$((passed + 1))
    else failed=$((failed + 1)); echo "FAIL $desc"; sed 's/^/     /' "$tmp/out"; fi
}
same_output() { "$@" >"$tmp/got" 2>&1 && cmp -s "$tmp/got" "$expect"; }
absent() { [ ! -e "$1" ]; }

check "-S writes program.masm and does not run it" \
    sh -c '"$1" -S "$2" >"$4" && [ -f "$3" ] && [ ! -s "$4" ]' _ "$driver" "$src" "$masm" "$tmp/s.out"
check "the exported assembly runs on its own" same_output "$MLWSVM" run "$masm"
check "the exported assembly assembles to .mlwso" "$MLWSVM" asm "$masm" -o "$tmp/prog.mlwso"
check "the .mlwso runs" same_output "$MLWSVM" run "$tmp/prog.mlwso"
rm -f "$masm"
check "-S -o - writes the assembly to stdout" \
    sh -c '"$1" -S -o - "$2" >"$3" && grep -q "^\.func main" "$3"' _ "$driver" "$src" "$tmp/stdout.masm"
check "... and nothing next to the source" absent "$masm"
check "-S -o FILE writes FILE" sh -c '"$1" -S -o "$2" "$3" && [ -f "$2" ]' _ "$driver" "$tmp/custom.masm" "$src"
check "-k runs the program" same_output "$driver" -k "$src"
check "... and keeps program.masm" test -f "$masm"
rm -f "$masm"
check "a plain run" same_output "$driver" "$src"
check "... leaves no assembly behind" absent "$masm"
check "-o - without -S is refused" sh -c '! "$1" -o - "$2"' _ "$driver" "$src"

echo "drivers: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
