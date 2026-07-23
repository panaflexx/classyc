#!/bin/sh
# test.sh — boot the http_crud server, exercise every route with curl,
# then shut it down.
#
# Usage: ./test.sh [fibers|blocking|workers] [port] [nworkers]
set -u

MODE="${1:-fibers}"
PORT="${2:-8080}"
NWORKERS="${3:-4}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CLASSYC="$ROOT/build/bin/classyc"
LOG="/tmp/http_crud-$MODE-$PORT.log"
BASE="http://127.0.0.1:$PORT"

MODEARGS=""
case "$MODE" in
    fibers)  MODEARGS="fibers" ;;
    workers) MODEARGS="workers --workers=$NWORKERS" ;;
    blocking) MODEARGS="" ;;
    *)
        echo "usage: $0 [fibers|blocking|workers] [port] [nworkers]"
        exit 2
        ;;
esac

echo "== http_crud integration test: $MODE server on :$PORT (log: $LOG)"

# shellcheck disable=SC2086
"$CLASSYC" -I "$ROOT/include" -I "$ROOT/ext/ccchan" -ffibers -l sqlite3 \
    "$ROOT/examples/http-serve.c" "$ROOT/examples/http-serve-fibers.c" \
    "$ROOT/examples/http-serve-workers.cy" \
    "$HERE/main.cy" "$HERE/items.cy" \
    -eg -- $MODEARGS --port="$PORT" >"$LOG" 2>&1 &
SRV=$!

stop_server() {
    # Fiber/worker servers can be in a tight poll loop; TERM then hard KILL.
    kill -TERM "$SRV" 2>/dev/null
    sleep 0.3
    kill -KILL "$SRV" 2>/dev/null
    wait "$SRV" 2>/dev/null
    return 0
}
trap 'stop_server' EXIT

# Wait for the server to accept connections (JIT compile takes a moment).
i=0
while [ "$i" -lt 150 ]; do
    if curl -sf --max-time 1 "$BASE/health" >/dev/null 2>&1; then break; fi
    if ! kill -0 "$SRV" 2>/dev/null; then
        echo "server died at startup; log follows:"
        cat "$LOG"
        exit 1
    fi
    sleep 0.2
    i=$((i + 1))
done
if [ "$i" -ge 150 ]; then
    echo "server did not start in time; log follows:"
    cat "$LOG"
    exit 1
fi

fails=0
check() { # name expected-substring curl-args...  (appends HTTP status to body)
    name="$1"; want="$2"; shift 2
    got="$(curl -s --max-time 5 -w '\n%{http_code}' "$@")"
    if printf '%s' "$got" | grep -qF "$want"; then
        printf '  ok    %-22s %s\n' "$name" "$(printf '%s' "$got" | head -1)"
    else
        printf '  FAIL  %-22s %s   (wanted: %s)\n' "$name" "$got" "$want"
        fails=$((fails + 1))
    fi
}

check "GET /health"        '"ok":true'            "$BASE/health"
check "GET /api/items"     '"total":2'            "$BASE/api/items"
check "GET ?q=Widget"      '"total":1'            "$BASE/api/items?q=Widget"
check "GET /api/items/1"   '"name":"Widget"'      "$BASE/api/items/1"
check "GET /api/items/999" 'not found'            "$BASE/api/items/999"
check "POST /api/items"    '"name":"Sprocket"'    -X POST -d '{"name":"Sprocket","qty":5}' "$BASE/api/items"
check "PUT /api/items/1"   '"qty":99'             -X PUT  -d '{"qty":99}' "$BASE/api/items/1"
check "DELETE /api/items/2" '204'               -X DELETE "$BASE/api/items/2"

# Concurrent fan-out smoke (fibers + workers).  Background curls + wait;
# each curl has --max-time so a stuck peer cannot hang forever.
if [ "$MODE" = "fibers" ] || [ "$MODE" = "workers" ]; then
    conc_fail=0
    pids=""
    for n in 1 2 3 4 5 6 7 8; do
        ( curl -sf --max-time 3 "$BASE/health" >/dev/null 2>&1 || exit 1 ) &
        pids="$pids $!"
    done
    for p in $pids; do
        if ! wait "$p"; then conc_fail=$((conc_fail + 1)); fi
    done
    if [ "$conc_fail" -eq 0 ]; then
        printf '  ok    %-22s %s\n' "concurrent x8" "health"
    else
        printf '  FAIL  %-22s %s\n' "concurrent x8" "$conc_fail failed"
        fails=$((fails + 1))
    fi
fi

echo
if [ "$fails" -eq 0 ]; then
    echo "PASS ($MODE)"
else
    echo "FAIL ($MODE): $fails check(s) failed"
fi
exit "$fails"
