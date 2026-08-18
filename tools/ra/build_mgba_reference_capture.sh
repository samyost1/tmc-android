#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
prefix=${MGBA_PREFIX:-/opt/homebrew/opt/mgba}
cc=${CC:-clang}

exec "$cc" -std=c11 -O2 -Wall -Wextra \
    -I"$prefix/include" \
    -I"$root/tools/ra" \
    -I"$root/port/ra" \
    "$root/tools/ra/mgba_reference_capture.c" \
    -L"$prefix/lib" -lmgba \
    -Wl,-rpath,"$prefix/lib" \
    -o "$root/tools/ra/mgba_reference_capture"
