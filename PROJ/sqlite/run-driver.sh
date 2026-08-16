#!/bin/sh
# Smoke-test SQLite 3.46.1 amalgamation with ClassyC.
#
# -fno-exceptions is still needed for JIT (-eg): MIR codegen hits an SSA
# assert when exception guards are on.  Trailing `T a[1]` FAMs are now
# recognized (interpreter -ei works with default-on guards).
#
# Usage (from repo root):
#   sh PROJ/sqlite/run-driver.sh           # interpreter
#   sh PROJ/sqlite/run-driver.sh -eg       # MIR gen / JIT
#   sh PROJ/sqlite/run-driver.sh -el       # lazy gen

set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLASSYC="${CLASSYC:-$ROOT/build/bin/classyc}"
if [ ! -x "$CLASSYC" ]; then
  CLASSYC="$ROOT/bin/classyc"
fi
MODE="${1:--ei}"
cd "$(dirname "$0")"
exec "$CLASSYC" -fno-exceptions -I. sqlite3.c driver.c "$MODE"
