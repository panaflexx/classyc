#!/bin/sh
# cy-bench/run-bench.sh — run the Phase H benchmark suite.
# Usage: sh cy-bench/run-bench.sh [path-to-classyc]
#
# Prints per-phase timings.  For A/B comparisons of header/compiler changes,
# interleave runs (see GEN-OPT-FINDINGS.md "Phase H") — absolute numbers are
# load-sensitive; ratios from interleaved runs are not.

CLASSYC=${1:-./bin/classyc}
set -e

for b in cy-bench/bench-list-pipeline.cy cy-bench/bench-map-probes.cy; do
    echo "########## $b"
    "$CLASSYC" -I include "$b" -eg
done
