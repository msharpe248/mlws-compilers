#!/bin/sh
# Make the expected output of Lua tests with the reference lua 5.5:
# NAME.out (stdout) and, if it fails, NAME.err (stderr's first line, with
# lua's "lua:" as "mlws-vm-lua:").
#   tests/expect.sh [test.lua...]     (default: every test without a .out)
LUA=${LUA:-lua}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ $# -eq 0 ]; then
    set --
    for f in $(find "$here" -name '*.lua' ! -path '*/drivers/*' | sort); do
        [ -f "${f%.lua}.out" ] || set -- "$@" "$f"
    done
fi
for f in "$@"; do
    dir=$(dirname -- "$f") name=$(basename -- "$f" .lua)
    args=
    [ -f "$dir/$name.args" ] && args=$(cat "$dir/$name.args")
    input=/dev/null
    [ -f "$dir/$name.in" ] && input="$dir/$name.in"
    (CDPATH= cd -- "$dir" && $LUA "$name.lua" $args <"$input" >"$name.out" 2>"$name.stderr")
    if [ -s "$dir/$name.stderr" ]; then
        head -n 1 "$dir/$name.stderr" | sed 's/^lua: /mlws-vm-lua: /' >"$dir/$name.err"
    else
        rm -f "$dir/$name.err"
    fi
    rm -f "$dir/$name.stderr"
    echo "$f"
done
