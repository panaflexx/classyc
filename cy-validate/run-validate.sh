#!/bin/sh
# run-validate.sh — run the ClassyC README validation suite.
#
# Each val-*.cy is a self-checking program that prints PASS/FAIL lines and
# returns the number of failed assertions as its exit code (0 == all passed).
#
# Usage:  sh cy-validate/run-validate.sh [path/to/classyc]
#
# Run from the project root so the `-I include` path resolves. Tests are built
# with -g (debug info, so gdb works) and JIT-run with -eg, exactly as the task
# requires:  ./bin/classyc -g -I include <file>.cy -eg

CLASSYC="${1:-./bin/classyc}"
DIR="$(dirname "$0")"
INCLUDE="-I include"
PASS=0
FAIL=0
CRASH=0

banner() {
    line=$(printf '=%.0s' 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60)
    printf "\n%s\n  %s\n%s\n" "$line" "$1" "$line"
}

for f in "$DIR"/val-*.cy; do
    [ -f "$f" ] || continue
    name=$(basename "$f")
    banner "$name"
    $CLASSYC -g $INCLUDE "$f" -eg
    status=$?
    if [ $status -eq 0 ]; then
        PASS=$((PASS + 1))
    elif [ $status -ge 132 ]; then
        printf "\n[CRASHED: %s (signal, exit %d)]\n" "$name" "$status"
        CRASH=$((CRASH + 1))
    else
        printf "\n[FAILED: %s (%d failed assertions)]\n" "$name" "$status"
        FAIL=$((FAIL + 1))
    fi
done

line=$(printf '=%.0s' 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60)
printf "\n%s\n" "$line"
printf "  validation files: %d passed, %d failed, %d crashed\n" "$PASS" "$FAIL" "$CRASH"
printf "%s\n" "$line"

exit $((FAIL + CRASH))
