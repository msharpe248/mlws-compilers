#!/usr/bin/env python3
"""Run a compiler's test suite: compile each test program, run it on MLWSVM and
compare the results with the expectations.

    run-tests.py bf  DIR...     brainfuck tests (see bf/tests)
    run-tests.py lox DIR...     Lox tests in the Crafting Interpreters format
    run-tests.py lua DIR...     Lua tests (see lua/tests)

brainfuck: NAME.b is the program, NAME.out the expected stdout (exact bytes),
and optionally NAME.in (stdin), NAME.opts (mlws-vm-bfc options), NAME.err (a string
that stderr must contain) and NAME.exit (the expected exit code, default 0).

Lox: the comments in each .lox file give the expectations, as in the test
suite of https://github.com/munificent/craftinginterpreters:
    // expect: OUTPUT                one line of stdout
    // expect runtime error: MSG     stderr's first line; the next has [line N]
    // Error ...  / // [line N] Error ...  / // [java line N] Error ...
                                     a compile error (exit code 65)
Files containing "// nontest" are skipped, and so are the paths listed in
DIR/SKIP (one per line, relative to DIR).

Lua: NAME.lua runs (from its own directory) and NAME.out is the expected
stdout, as the reference lua 5.5 prints it (lua/tests/expect.sh makes it);
NAME.err, if present, is the expected first line of stderr ("mlws-vm-lua:
..." where lua says "lua: ..."), with exit status 1. NAME.in is stdin and
NAME.args the arguments, one per line.

Options: -v prints the output of failing tests, -j N runs N tests at once,
and a FILTER argument (substring of the path) after the directories selects
tests.
"""
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def find_vm():
    vm = os.environ.get("MLWSVM")
    if vm:
        return vm
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = os.path.join(d, "mlwsvm")
        if os.access(p, os.X_OK):
            return p
    return os.path.normpath(os.path.join(ROOT, "..", "mlws-vm", "mlwsvm"))


VM = find_vm()
TIMEOUT = 60


def run(cmd, stdin=b""):
    try:
        r = subprocess.run(cmd, input=stdin, capture_output=True, timeout=TIMEOUT)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return None, b"", b"timeout"


# ------------------------------------------------------------------ brainfuck

def bf_test(path, tmp):
    stem = path[:-2]

    def opt(ext, binary=False):
        p = stem + ext
        if not os.path.exists(p):
            return None
        with open(p, "rb") as f:
            data = f.read()
        return data if binary else data.decode().strip()

    masm = os.path.join(tmp, os.path.basename(stem) + ".masm")
    opts = (opt(".opts") or "").split()
    code, _, err = run([os.path.join(ROOT, "bf", "mlws-vm-bfc")] + opts + ["-o", masm, path])
    if code != 0:
        return ["mlws-vm-bfc failed: " + err.decode(errors="replace")]
    code, out, err = run([VM, "run", masm], opt(".in", True) or b"")
    fails = []
    want_exit = int(opt(".exit") or 0)
    if code != want_exit:
        fails.append(f"exit code {code}, expected {want_exit}")
    want_out = opt(".out", True)
    if want_out is not None and out != want_out:
        fails.append(f"stdout differs: got {out[:200]!r}, expected {want_out[:200]!r}")
    want_err = opt(".err")
    if want_err and want_err.encode() not in err:
        fails.append(f"stderr {err[:200]!r} does not contain {want_err!r}")
    elif not want_err and err:
        fails.append(f"unexpected stderr {err[:200]!r}")
    return fails


# ------------------------------------------------------------------------ Lox

EXPECT_OUT = re.compile(r"// expect: ?(.*)")
EXPECT_ERR = re.compile(r"// (Error.*)")
EXPECT_ERR_LINE = re.compile(r"// \[((java|c) )?line (\d+)\] (Error.*)")
EXPECT_RUNTIME = re.compile(r"// expect runtime error: (.+)")
SYNTAX_ERR = re.compile(r"\[.*line (\d+)\] (Error.+)")
STACK_LINE = re.compile(r"\[line (\d+)\]")
# Which variant's cascaded compile errors to expect: our parser recovers from
# errors the way jlox does.
LANGUAGE = "java"


def lox_test(path, tmp):
    out_lines, errors, runtime, runtime_line, want_exit = [], set(), None, 0, 0
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    for n, line in enumerate(lines, 1):
        if "// nontest" in line:
            return None
        m = EXPECT_OUT.search(line)
        if m:
            out_lines.append(m.group(1))
            continue
        m = EXPECT_ERR.search(line)
        if m:
            errors.add(f"[{n}] {m.group(1)}")
            want_exit = 65
            continue
        m = EXPECT_ERR_LINE.search(line)
        if m:
            if m.group(2) is None or m.group(2) == LANGUAGE:
                errors.add(f"[{m.group(3)}] {m.group(4)}")
                want_exit = 65
            continue
        m = EXPECT_RUNTIME.search(line)
        if m:
            runtime, runtime_line, want_exit = m.group(1), n, 70

    code, out, err = run([os.path.join(ROOT, "lox", "mlws-vm-lox"), path])
    out = out.decode(errors="replace").split("\n")
    err = err.decode(errors="replace").split("\n")
    if out and out[-1] == "":
        out.pop()
    if err and err[-1] == "":
        err.pop()

    fails = []
    if runtime is not None:
        if len(err) < 2:
            fails.append(f"expected runtime error '{runtime}' and got none")
        else:
            if err[0] != runtime:
                fails.append(f"expected runtime error '{runtime}' and got '{err[0]}'")
            m = None
            for l in err[1:]:
                m = STACK_LINE.search(l)
                if m:
                    break
            if not m:
                fails.append("expected a stack trace and got: " + " | ".join(err[1:]))
            elif int(m.group(1)) != runtime_line:
                fails.append(f"expected runtime error on line {runtime_line} but was on line {m.group(1)}")
    else:
        found = set()
        for l in err:
            m = SYNTAX_ERR.search(l)
            if m:
                e = f"[{m.group(1)}] {m.group(2)}"
                if e in errors:
                    found.add(e)
                else:
                    fails.append("unexpected error: " + l)
            elif l != "":
                fails.append("unexpected output on stderr: " + l)
        for e in sorted(errors - found):
            fails.append("missing expected error: " + e)
    if code != want_exit:
        fails.append(f"expected exit code {want_exit} and got {code}")
    for i, l in enumerate(out):
        if i >= len(out_lines):
            fails.append(f"got output '{l}' when none was expected")
        elif out_lines[i] != l:
            fails.append(f"expected output '{out_lines[i]}' and got '{l}'")
    for l in out_lines[len(out):]:
        fails.append(f"missing expected output '{l}'")
    return fails


# ------------------------------------------------------------------------ Lua

def lua_test(path, tmp):
    stem = path[:-4]

    def opt(ext):
        p = stem + ext
        if not os.path.exists(p):
            return None
        with open(p, "rb") as f:
            return f.read()

    want_out = opt(".out")
    if want_out is None:
        return ["no " + os.path.basename(stem) + ".out (run lua/tests/expect.sh)"]
    want_err = opt(".err")
    args = (opt(".args") or b"").decode().splitlines()
    driver = os.path.join(ROOT, "lua", "mlws-vm-lua")
    try:
        r = subprocess.run([driver, os.path.basename(path)] + args, input=opt(".in") or b"",
                           capture_output=True, timeout=TIMEOUT, cwd=os.path.dirname(path) or ".")
        code, out, err = r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return ["timeout"]
    fails = []
    want_exit = 1 if want_err is not None else 0
    if code != want_exit:
        fails.append(f"exit code {code}, expected {want_exit}")
    if out != want_out:
        got, want = out.decode(errors="replace").split("\n"), want_out.decode(errors="replace").split("\n")
        for i in range(max(len(got), len(want))):
            g = got[i] if i < len(got) else "<none>"
            w = want[i] if i < len(want) else "<none>"
            if g != w:
                fails.append(f"stdout line {i + 1}: got {g[:200]!r}, expected {w[:200]!r}")
                break
    first = err.decode(errors="replace").split("\n")[0] if err else ""
    if first.startswith("mlws-vm-luac: "):   # a compile error
        first = "mlws-vm-lua: " + first[len("mlws-vm-luac: "):]
    if want_err is not None:
        w = want_err.decode(errors="replace").split("\n")[0]
        if first != w:
            fails.append(f"stderr {first[:200]!r}, expected {w[:200]!r}")
    elif err:
        fails.append(f"unexpected stderr {first[:200]!r}")
    return fails


# ----------------------------------------------------------------------- main

def collect(dirs, ext, filters):
    tests = []
    skips = []  # (directory, entries of its SKIP file)
    for d in dirs:
        for root, _, files in os.walk(d):
            if "SKIP" in files:
                with open(os.path.join(root, "SKIP")) as f:
                    skips.append((root, {l.split("#")[0].strip() for l in f} - {""}))
            for name in sorted(files):
                if not name.endswith(ext):
                    continue
                p = os.path.join(root, name)
                skipped = False
                for sd, entries in skips:
                    rel = os.path.relpath(p, sd)
                    if any(rel == s or rel.startswith(s.rstrip("/") + "/") for s in entries):
                        skipped = True
                if skipped:
                    continue
                if filters and not any(f in p for f in filters):
                    continue
                tests.append(p)
    return sorted(tests)


def main():
    args = sys.argv[1:]
    verbose = "-v" in args
    jobs = os.cpu_count() or 4
    if "-j" in args:
        i = args.index("-j")
        jobs = int(args[i + 1])
        del args[i:i + 2]
    args = [a for a in args if a != "-v"]
    if not args or args[0] not in ("bf", "lox", "lua"):
        print(__doc__)
        sys.exit(2)
    kind = args[0]
    dirs = [a for a in args[1:] if os.path.isdir(a)]
    filters = [a for a in args[1:] if not os.path.isdir(a)]
    ext, fn = {"bf": (".b", bf_test), "lox": (".lox", lox_test), "lua": (".lua", lua_test)}[kind]
    tests = collect(dirs, ext, filters)

    passed = failed = skipped = 0
    with tempfile.TemporaryDirectory() as tmp, \
            concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for path, fails in zip(tests, pool.map(lambda p: fn(p, tmp), tests)):
            if fails is None:
                skipped += 1
            elif fails:
                failed += 1
                print(f"FAIL {path}")
                for f in fails[: None if verbose else 5]:
                    print("     " + f)
            else:
                passed += 1
    print(f"{passed} passed, {failed} failed" + (f", {skipped} skipped" if skipped else ""))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
