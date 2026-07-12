#!/bin/bash
# Run all ClassyC examples under examples/*.cy
#
# Most examples should compile and exit 0.  Examples declare intent with:
#
#     // @expect: pass     compiles and exits 0                  (default)
#     // @expect: fail     clean error exit (compile or run 1..127)
#     // @expect: crash    signal termination (exit >= 128)
#     // @expect: skip     do not run from this harness
#
# First match wins.  Files without a directive default to pass.
#
# Hard-coded skips (no @expect required): webmain.cy (multi-TU server).
# Special env: classy-docsearch.cy runs with DOCSEARCH_BATCH=1 DOCSEARCH_MAX=80.
#
# From repo root:
#   ./examples/run-examples.sh
#   ./run-examples.sh          # thin wrapper → this script

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

CLASSYC="${CLASSYC:-bin/classyc}"
if [ ! -x "$CLASSYC" ]; then
    echo "error: $CLASSYC not found or not executable (build classyc first)" >&2
    exit 127
fi

echo "Running ClassyC examples from $ROOT (using $CLASSYC)..."
echo ""

SEGFAULTS=()
PROBLEMS=()
SKIPPED=()
UNEXPECTED=()
PASSED=0

expectation_for() {
    local file="$1" line
    # basename hard skips
    case "$(basename "$file")" in
        webmain.cy|webmake.cy)
            echo "skip"
            return
            ;;
    esac
    line=$(grep -m1 -oE '@expect:[[:space:]]*[a-zA-Z]+' "$file" 2>/dev/null || true)
    case "$line" in
        *fail*)  echo "fail"  ;;
        *crash*) echo "crash" ;;
        *skip*)  echo "skip"  ;;
        *)       echo "pass"  ;;
    esac
}

# True if status indicates termination by signal (bash: 128 + signo).
is_signal_exit() {
    local s="$1"
    [ "$s" -ge 128 ] 2>/dev/null
}

run_one() {
    local example="$1"
    local base
    base=$(basename "$example")

    # Docsearch: non-interactive smoke (tiny cap so the harness stays quick).
    if [ "$base" = "classy-docsearch.cy" ]; then
        DOCSEARCH_BATCH=1 DOCSEARCH_MAX=80 DOCSEARCH_CDB=/tmp/classy-docsearch-harness.cdb \
            "$CLASSYC" -I include -g "$example" -eg
        return $?
    fi

    # SQLite examples need -l sqlite3; harmless for others that don't resolve it.
    "$CLASSYC" -I include -l sqlite3 -g "$example" -eg
    return $?
}

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
    set +e
    run_one "$example"
    status=$?
    set -e

    crashed=0
    is_signal_exit "$status" && crashed=1

    case "$expect" in
        pass)
            if [ "$status" -eq 0 ]; then
                PASSED=$((PASSED + 1))
            elif [ "$crashed" -eq 1 ]; then
                SEGFAULTS+=("$example (exit $status)")
            else
                PROBLEMS+=("$example (exit $status)")
            fi
            ;;
        fail)
            if [ "$status" -eq 0 ]; then
                UNEXPECTED+=("$example (expected fail, got 0)")
            elif [ "$crashed" -eq 1 ]; then
                SEGFAULTS+=("$example (expected clean fail, got signal exit $status)")
            else
                PASSED=$((PASSED + 1))
            fi
            ;;
        crash)
            if [ "$crashed" -eq 1 ]; then
                PASSED=$((PASSED + 1))
            elif [ "$status" -eq 0 ]; then
                UNEXPECTED+=("$example (expected crash, got 0)")
            else
                PROBLEMS+=("$example (expected crash, got exit $status)")
            fi
            ;;
    esac

    echo "Finished $example (exit $status, @expect: $expect)"
    echo "---"
done

echo ""
echo "All examples completed."
echo "  matched expectation: $PASSED"
echo "  skipped:             ${#SKIPPED[@]}"
echo "  problems:            ${#PROBLEMS[@]}"
echo "  unexpected success:  ${#UNEXPECTED[@]}"
echo "  unexpected crash:    ${#SEGFAULTS[@]}"

if [ ${#PROBLEMS[@]} -gt 0 ]; then
    echo ""
    echo "PROBLEMS (wrong non-zero / clean exit):"
    for example in "${PROBLEMS[@]}"; do
        echo "  $example"
    done
fi

if [ ${#UNEXPECTED[@]} -gt 0 ]; then
    echo ""
    echo "Unexpected successes (negative test that should have failed/crashed):"
    for example in "${UNEXPECTED[@]}"; do
        echo "  $example"
    done
fi

if [ ${#SEGFAULTS[@]} -gt 0 ]; then
    echo ""
    echo "Unexpected signal terminations (segfault/abort/…):"
    for example in "${SEGFAULTS[@]}"; do
        echo "  $example"
    done
else
    echo ""
    echo "No samples crashed unexpectedly."
fi

if [ ${#SKIPPED[@]} -gt 0 ]; then
    echo ""
    echo "Skipped (run manually — see each file's header):"
    for example in "${SKIPPED[@]}"; do
        echo "  $example"
    done
fi

total_bad=$(( ${#PROBLEMS[@]} + ${#SEGFAULTS[@]} + ${#UNEXPECTED[@]} ))
# Cap at 255 for shell exit codes
if [ "$total_bad" -gt 255 ]; then
    total_bad=255
fi
exit "$total_bad"
