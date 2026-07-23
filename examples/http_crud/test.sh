#!/bin/sh
# test.sh — boot the http_crud server, exercise every route with curl,
# then shut it down.  Usage: ./test.sh [fibers|blocking] [port]
set -u

MODE="${1:-fibers}"
PORT="${2:-8080}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CLASSYC="$ROOT/build/bin/classyc"
LOG="/tmp/http_crud-$MODE-$PORT.log"
BASE="http://127.0.0.1:$PORT"

MODEARG=""
[ "$MODE" = "fibers" ] && MODEARG="fibers"

echo "== http_crud integration test: $MODE server on :$PORT (log: $LOG)"

"$CLASSYC" -I "$ROOT/include" -I "$ROOT/ext/ccchan" -l sqlite3 \
    "$ROOT/examples/http-serve.c" "$ROOT/examples/http-serve-fibers.c" \
    "$HERE/main.cy" "$HERE/items.cy" \
    -eg -- $MODEARG --port="$PORT" >"$LOG" 2>&1 &
SRV=$!
trap 'kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null' EXIT

# Wait for the server to accept connections (JIT compile takes a moment).
i=0
while [ "$i" -lt 150 ]; do
    if curl -sf "$BASE/health" >/dev/null 2>&1; then break; fi
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
    got="$(curl -s -w '\n%{http_code}' "$@")"
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

echo
if [ "$fails" -eq 0 ]; then
    echo "PASS ($MODE)"
else
    echo "FAIL ($MODE): $fails check(s) failed"
fi
exit "$fails"
