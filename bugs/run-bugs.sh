#!/bin/sh
# run-bugs.sh — run the bugs/ regression programs (mirror cy-validate).
#
# Usage (from project root):
#   sh bugs/run-bugs.sh
#   sh bugs/run-bugs.sh ./bin/classyc
#
# Each bugs/*.cy is built with -g -I include and JIT-run with -eg.
# Exit status 0 = process exit 0; non-zero = failure; >=132 = crash/signal.

CLASSYC="${1:-./bin/classyc}"
DIR="$(cd "$(dirname "$0")" && pwd)"
INCLUDE="-I include"
PASS=0
FAIL=0
CRASH=0

banner() {
    line=$(printf '=%.0s' 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50)
    printf "\n%s\n  %s\n%s\n" "$line" "$1" "$line"
}

for f in "$DIR"/*.cy; do
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
        printf "\n[FAILED: %s (exit %d)]\n" "$name" "$status"
        FAIL=$((FAIL + 1))
    fi
done

printf "\nbugs: %d passed, %d failed, %d crashed\n" "$PASS" "$FAIL" "$CRASH"
exit $((FAIL + CRASH))
