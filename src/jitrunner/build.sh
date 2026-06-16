#!/usr/bin/env bash
#
# build.sh — build the classyc jitrunner
#
# The jitrunner is a hybrid:
#   - jitrunner.c is ClassyC code, compiled with classyc → .bmir → .o
#   - mir-bridge.c is plain C, compiled with gcc → .o
#   - Both are linked with libmir_static.a + system libs
#
# Usage (from project root):
#   bash src/jitrunner/build.sh [-v]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$ROOT"

VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1 && set -x

# Tools
CLASSYC="./bin/classyc"
B2OBJ="./bin/b2obj"
CC="${CC:-gcc}"

# Paths
SRC="src/jitrunner"
MIR_DIR="ext/mir"
INC_DIR="include"
LIB_DIR="lib"
OUT_DIR="bin"
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/jitrunner-build.XXXXXX")

cleanup() { [ "$VERBOSE" -eq 1 ] && echo "intermediates in $WORK_DIR" || rm -rf "$WORK_DIR"; }
trap cleanup EXIT

echo "=== Building classyc jitrunner ==="

# 1. Compile mir-bridge.c with gcc (needs real MIR headers + macros)
echo "[1/4] Compiling mir-bridge.c (gcc)..."
"$CC" -O2 -I "$MIR_DIR" -I "$INC_DIR" -c "$SRC/mir-bridge.c" -o "$WORK_DIR/mir-bridge.o"

# 2. Compile jitrunner.c with classyc → .bmir
echo "[2/4] Compiling jitrunner.c (classyc → bmir)..."
"$CLASSYC" -c -o "$WORK_DIR/jitrunner.bmir" "$SRC/jitrunner.c"

# 3. Convert .bmir → .o
echo "[3/4] Converting jitrunner.bmir → .o (b2obj)..."
"$B2OBJ" "$WORK_DIR/jitrunner.bmir" "$WORK_DIR/jitrunner.o"

# 4. Compile the AOT runtime if present
RT_OBJ=""
if [ -f "src/mir-aot-runtime.c" ]; then
    echo "[3.5/4] Compiling AOT runtime..."
    "$CC" -O2 -I "$INC_DIR" -c src/mir-aot-runtime.c -o "$WORK_DIR/mir-aot-runtime.o"
    RT_OBJ="$WORK_DIR/mir-aot-runtime.o"
fi

# 5. Link everything
echo "[4/4] Linking jitrunner..."
LINK_CMD=(
    "$CC" -no-pie -o "$OUT_DIR/jitrunner"
    "$WORK_DIR/jitrunner.o"
    "$WORK_DIR/mir-bridge.o"
)
[ -n "$RT_OBJ" ] && LINK_CMD+=("$RT_OBJ")
LINK_CMD+=(
    "$LIB_DIR/libmir_static.a"
    -lm -lpthread -ldl
)

"${LINK_CMD[@]}"

echo ""
echo "=== Built: $OUT_DIR/jitrunner ==="
echo ""
echo "Usage:"
echo "  # Run a .bmir file:"
echo "  $OUT_DIR/jitrunner path/to/program.bmir"
echo ""
echo "  # Watch mode (re-run on change):"
echo "  $OUT_DIR/jitrunner path/to/program.bmir --watch"
echo ""
echo "  # Compile + run + watch:"
echo "  $OUT_DIR/jitrunner --compile examples/classy-classes.c --watch"
