#!/bin/sh
# run-tests.sh — run all 100 test cases in cy-testhard directory
#
# Usage:  sh cy-testhard/run-tests.sh [path/to/classyc]

CLASSYC="${1:-./bin/classyc}"
DIR="$(dirname "$0")"
PASS=0
FAIL=0
CRASH=0

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

run_test() {
    name="$1"
    shift
    extra_flags="$*"
    banner "$name"
    $CLASSYC "$DIR/$name" -eg $extra_flags
    status=$?
    if [ $status -eq 0 ]; then
        PASS=$((PASS + 1))
    elif [ $status -eq 139 ]; then
        printf "\n[CRASHED: %s (segfault, exit %d)]\n" "$name" "$status"
        CRASH=$((CRASH + 1))
    else
        printf "\n[FAILED: %s exited non-zero (exit %d)]\n" "$name" "$status"
        FAIL=$((FAIL + 1))
    fi
}

# ── tests ───────────────────────────────────────────────────────────

# Generate and run all 100 test cases
for i in $(seq -f "%03g" 0 99); do
    run_test "cy-$i.c"
done

# ── summary ────────────────────────────────────────────────────────────

printf "\n%s\n" "$(printf '=%.0s' $(seq 1 60))"
printf "  results: %d passed, %d failed, %d crashed\n" "$PASS" "$FAIL" "$CRASH"
printf "%s\n" "$(printf '=%.0s' $(seq 1 60))"

exit $((FAIL + CRASH))
