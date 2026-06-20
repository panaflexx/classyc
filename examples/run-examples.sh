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

run_example classy.cy
run_example classy2.cy
run_example classy3.cy
run_example classy4.cy
run_example classy5.cy
run_example classy6.cy
run_example classy7.cy
run_example classy8.cy
run_example classy-classes.cy
run_example classy-defer.cy
run_example classy-dict.cy
run_example classy-dict-arena.cy
run_example classy-file.cy
run_example classy-file2.cy
run_example classy-strings.cy
run_example classy-string-split-join.cy
run_example classy-string-copy-test.cy
run_example string_methods_test.cy
run_example test_dict_arena.cy
run_example classy-arrays.cy
run_example classy-fstring.cy
run_example classy-auto.cy
run_example classy-brace-forin.cy
run_example classy-seq-lambdas.cy
#run_example classy-stacktrace.c #CRASHTEST

run_example test-class-subscript.cy
run_example test-generic-ptr-args.cy
#run_example test-stacktrace-deep.c #CRASHTEST
run_example test_json_int.cy
run_example test-ptr-arg-mini.cy
run_example classy-generics.cy
run_example classy-sets.cy
run_example classy-sets-myclass.cy
run_example classy-map.cy
run_example classy-search-engine.cy
run_example classy-collections-class.cy
run_example classy-overload.cy
run_example test-interface.cy
run_example test-any.cy
run_example test-any-arena.cy
run_example test-any-implicit.cy
run_example t_raii.cy
run_example classy-exceptions.cy
run_example classy-tolist.cy
run_example test-array-to-list.cy
run_example test-list-stdlib.cy

# ── summary ────────────────────────────────────────────────────────────

printf "\n%s\n" "$(printf '=%.0s' $(seq 1 60))"
printf "  results: %d passed, %d failed\n" "$PASS" "$FAIL"
printf "%s\n" "$(printf '=%.0s' $(seq 1 60))"

exit $FAIL
