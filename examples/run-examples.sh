#!/bin/bash

# Run all examples in the examples directory.
#
# Most examples are expected to compile and exit 0.  Some, however, are
# deliberately negative (they demonstrate a compile-time diagnostic), crash on
# purpose (the stack-trace demos), or can't be run stand-alone (multi-file
# units, CLI tools needing arguments).  Each such example declares its intent
# with an in-file directive comment:
#
#     // @expect: pass     compiles and exits 0                  (the default)
#     // @expect: fail     compiles/runs to a clean error exit   (1..131)
#     // @expect: crash    terminates via a signal/abort         (>=132)
#     // @expect: skip      do not run from this harness
#
# The directive may appear anywhere in the file (first match wins).  Examples
# without a directive are treated as `pass`.

echo "Running all ClassyC examples..."

SEGFAULTS=()   # crashed when we didn't expect it
PROBLEMS=()    # exited non-zero (or zero) against expectation
SKIPPED=()     # explicitly skipped
UNEXPECTED=()  # an @expect:fail/crash that unexpectedly succeeded

# Read the first `@expect:` directive from a file; default to "pass".
expectation_for() {
    local file="$1" line
    line=$(grep -m1 -oE '@expect:[[:space:]]*[a-zA-Z]+' "$file" 2>/dev/null)
    case "$line" in
        *fail*)  echo "fail"  ;;
        *crash*) echo "crash" ;;
        *skip*)  echo "skip"  ;;
        *)       echo "pass"  ;;
    esac
}

# Find all .cy files in examples directory
for example in examples/*.cy; do
    [ -f "$example" ] || continue

    expect=$(expectation_for "$example")

    if [ "$expect" = "skip" ]; then
        echo "Skipping $example (@expect: skip)"
        SKIPPED+=("$example")
        echo "---"
        continue
    fi

    echo "Running $example (@expect: $expect)..."
    bin/classyc -I include -l sqlite3 -g "$example" -eg
    status=$?

    crashed=0
    [ $status -ge 132 ] && crashed=1

    case "$expect" in
        pass)
            if [ $status -eq 0 ]; then
                : # OK
            elif [ $crashed -eq 1 ]; then
                SEGFAULTS+=("$example")
            else
                PROBLEMS+=("$example")
            fi
            ;;
        fail)
            if [ $status -eq 0 ]; then
                UNEXPECTED+=("$example (expected a failure, but it succeeded)")
            elif [ $crashed -eq 1 ]; then
                SEGFAULTS+=("$example (expected a clean failure, got a crash)")
            else
                : # OK: expected non-zero, got non-zero
            fi
            ;;
        crash)
            if [ $crashed -eq 1 ]; then
                : # OK: expected a crash, got one
            elif [ $status -eq 0 ]; then
                UNEXPECTED+=("$example (expected a crash, but it succeeded)")
            else
                PROBLEMS+=("$example (expected a crash, got a clean non-zero exit)")
            fi
            ;;
    esac

    echo "Finished $example (exit $status, @expect: $expect)"
    echo "---"
done

echo "All examples completed."

echo ""
echo "PROBLEMS:"
for example in "${PROBLEMS[@]}"; do
    echo "  $example"
done

if [ ${#UNEXPECTED[@]} -gt 0 ]; then
    echo ""
    echo "Unexpected successes (negative test that should have failed):"
    for example in "${UNEXPECTED[@]}"; do
        echo "  $example"
    done
fi

if [ ${#SEGFAULTS[@]} -gt 0 ]; then
    echo ""
    echo "Samples that segfaulted:"
    for example in "${SEGFAULTS[@]}"; do
        echo "  $example"
    done
else
    echo ""
    echo "No samples segfaulted unexpectedly."
fi

if [ ${#SKIPPED[@]} -gt 0 ]; then
    echo ""
    echo "Skipped (run manually — see each file's header):"
    for example in "${SKIPPED[@]}"; do
        echo "  $example"
    done
fi

# Non-zero exit if anything went against expectations.
total_bad=$(( ${#PROBLEMS[@]} + ${#SEGFAULTS[@]} + ${#UNEXPECTED[@]} ))
exit $total_bad
