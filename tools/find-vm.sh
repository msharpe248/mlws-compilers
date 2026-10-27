# Sourced by the compiler drivers: sets MLWSVM to the mlwsvm executable.
if [ -z "$MLWSVM" ]; then
    if command -v mlwsvm >/dev/null 2>&1; then
        MLWSVM=$(command -v mlwsvm)
    else
        repo=$(cd "$(dirname "$0")/.." && pwd)
        MLWSVM="$repo/../mlws-vm/mlwsvm"
    fi
fi
if [ ! -x "$MLWSVM" ]; then
    echo "$(basename "$0"): cannot find the mlwsvm executable; set MLWSVM=/path/to/mlwsvm" >&2
    exit 127
fi
