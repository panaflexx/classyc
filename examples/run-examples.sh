#!/bin/sh
# run-examples.sh — run all examples in this directory
#
# Usage:  sh examples/run-examples.sh [path/to/c2m]
#
# Each example is compiled and run with -eg.  A banner is printed before
# each one so output is easy to scan.  The script exits with the number
# of examples that failed.

C2M="${1:-./c2m}"
DIR="$(dirname "$0")"
PASS=0
FAIL=0

# ── helpers ────────────────────────────────────────────────────────────

banner() {
    name="$1"
    width=60
    label="  $name  "
    pad=$(( (width - ${#label}) / 2 ))
    line=$(printf '=%.0s' $(seq 1 $width))
    printf "\n%s\n" "$line"
    printf "%*s%s\n" "$pad" "" "$label"
    printf "%s\n\n" "$line"
}

run_example() {
    name="$1"
    shift
    extra_flags="$*"
    banner "$name"
    if $C2M "$DIR/$name" -eg $extra_flags; then
        PASS=$((PASS + 1))
    else
        printf "\n[FAILED: %s exited non-zero]\n" "$name"
        FAIL=$((FAIL + 1))
    fi
}

# ── examples ───────────────────────────────────────────────────────────

run_example classy.c
run_example classy2.c
run_example classy3.c
run_example classy4.c
run_example classy5.c
run_example classy6.c
run_example classy7.c
run_example classy8.c
run_example classy-classes.c
run_example classy-defer.c
run_example classy-dict.c
run_example classy-dict-arena.c
run_example classy-file.c
run_example classy-file2.c
run_example classy-strings.c
run_example classy-string-copy-test.c
run_example string_methods_test.c
run_example test_dict_arena.c

# ── summary ────────────────────────────────────────────────────────────

printf "\n%s\n" "$(printf '=%.0s' $(seq 1 60))"
printf "  results: %d passed, %d failed\n" "$PASS" "$FAIL"
printf "%s\n" "$(printf '=%.0s' $(seq 1 60))"

exit $FAIL
