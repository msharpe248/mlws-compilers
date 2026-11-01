#!/bin/sh
# Make the expected output of Erlang tests with the reference Erlang/OTP:
# NAME.out (stdout) for each NAME.erl that has a main/0. Other modules in a
# test's directory (helpers) are compiled with it.
#   tests/expect.sh [test.erl...]     (default: every test without a .out)
ERLC=${ERLC:-erlc}
ERL=${ERL:-erl}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ $# -eq 0 ]; then
    set --
    for f in $(find "$here" -name '*.erl' | sort); do
        grep -q '^main()' "$f" || continue
        [ -f "${f%.erl}.out" ] || set -- "$@" "$f"
    done
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/expect.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
for f in "$@"; do
    dir=$(CDPATH= cd -- "$(dirname -- "$f")" && pwd) name=$(basename -- "$f" .erl)
    rm -f "$tmp"/*.beam
    helpers=$(grep -L '^main()' "$dir"/*.erl)
    $ERLC -o "$tmp" "$f" $helpers || continue
    (CDPATH= cd -- "$dir" && $ERL -noshell -pa "$tmp" -eval "$name:main(), init:stop()." \
        </dev/null >"$name.out" 2>/dev/null)
    echo "$f"
done
