#!/bin/sh
# run-examples.sh — run all examples in this directory
#
# Usage:  sh examples/run-examples.sh [path/to/c2m]
#
# Each example is compiled and run with -eg.  A banner is printed before
# each one so output is easy to scan.  The script exits with the number
# of examples that failed.

CLASSYC="${1:-./bin/classyc}"
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
    if $CLASSYC "$DIR/$name" -eg $extra_flags; then
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
run_example class-arrays.c
run_example classy-fstring.c
run_example classy-auto.c
run_example classy-brace-forin.c
run_example classy-seq-lambdas.c
#run_example classy-stacktrace.c #CRASHTEST

run_example test-class-subscript.c
run_example test-generic-ptr-args.c
#run_example test-stacktrace-deep.c #CRASHTEST
run_example test_json_int.c
run_example test-ptr-arg-mini.c
run_example classy-generics.c
run_example classy-overload.c
run_example t_raii.c

# ── summary ────────────────────────────────────────────────────────────

printf "\n%s\n" "$(printf '=%.0s' $(seq 1 60))"
printf "  results: %d passed, %d failed\n" "$PASS" "$FAIL"
printf "%s\n" "$(printf '=%.0s' $(seq 1 60))"

exit $FAIL
