#!/bin/sh
# Phase A/B measurement harness for midopt (GEN-OPT.md).
# Run from repo root:  sh sketch/run-midopt-bench.sh [path/to/classyc]

set -e
CLASSYC="${1:-./bin/classyc}"
INC="-I include"
SUM=sketch/sketch-midopt-list-sum.cy
OOB=sketch/sketch-midopt-oob-elide.cy
TMP=${TMPDIR:-/tmp}

banner() {
  printf "\n======== %s ========\n" "$1"
}

run_jit() {
  label="$1"
  shift
  banner "$label"
  # shellcheck disable=SC2086
  $CLASSYC $INC -v -fdump-mir-stats "$@" -eg 2>&1 | tee /tmp/midopt-run.log | tail -40
  # extract mir-stats + midopt + timings lines
  grep -E '\[midopt\]|\[mir-stats\]|timings' /tmp/midopt-run.log || true
}

run_aot_size() {
  label="$1"
  out="$2"
  shift 2
  banner "AOT $label"
  # shellcheck disable=SC2086
  $CLASSYC $INC -c -o "$out" "$@" 2>&1 | tail -5
  if [ -f "$out" ]; then
    ls -la "$out"
    # count "func" lines in binary MIR if m2b available
    if [ -x ./bin/m2b ]; then
      ./bin/m2b "$out" 2>/dev/null | grep -c ': *func' || true
      echo "(func item count via m2b)"
    fi
  fi
}

banner "correctness"
$CLASSYC $INC "$SUM" -eg
$CLASSYC $INC -fno-midopt "$SUM" -eg
$CLASSYC $INC "$OOB" -eg

run_jit "JIT default (midopt on, exceptions on)" "$SUM"
run_jit "JIT -fno-midopt" -fno-midopt "$SUM"
run_jit "JIT -fno-exceptions -O2" -fno-exceptions -O2 "$SUM"
run_jit "JIT -fno-midopt -fno-exceptions -O2" -fno-midopt -fno-exceptions -O2 "$SUM"

run_aot_size "midopt" "$TMP/midopt-list.bmir" "$SUM"
run_aot_size "no-midopt" "$TMP/nomidopt-list.bmir" -fno-midopt "$SUM"

if [ -f "$TMP/midopt-list.bmir" ] && [ -f "$TMP/nomidopt-list.bmir" ]; then
  banner "AOT size delta"
  ls -la "$TMP/midopt-list.bmir" "$TMP/nomidopt-list.bmir"
fi

echo
echo "Done. See GEN-OPT-FINDINGS.md for recorded numbers."
