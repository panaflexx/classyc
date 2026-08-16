/* http_crud — tiny inventory API, attribute-routed.
 *
 * Controllers (items.cy) register handlers with [[HttpGet]] / [[HttpPost]] / …
 * This file is only the entry point: dispatch + boot + server mode selection.
 *
 * Modes:
 *   test       in-process CRUD (no sockets)
 *   blocking   serve(port)                — single-thread accept → handle
 *   fibers     serve_workers_cchan(port,1)— 1 dedicated pthread worker
 *   workers    serve_workers(port,N)      — multi-OS-thread pool + Chan fan-out
 *
 * "fibers" used to mean serve_fibers(port) from http-serve-fibers.c: one
 * minicoro coroutine per connection, cooperatively multiplexed on a single
 * OS thread via non-blocking sockets. That file #includes ext/ccchan/
 * minicoro.h directly, which classyc's parser can't accept (a hand-written
 * top-level __asm__ block for the coroutine context switch) -- and MIR has
 * no mechanism to emit raw inline assembly at all, so this isn't just a
 * parser gap. serve_workers_cchan(port, 1) (examples/http-serve-cchan.c) is
 * the nearest available substitute: it still keeps request handling off the
 * accept thread, just serially on one dedicated worker rather than
 * N-way-interleaved on the accept thread itself. Real N-way concurrency
 * still lives in "workers" mode.
 *
 * Self-test (no sockets):
 *
 *   ./bin/classyc -I include -I ext/ccchan -ffibers -l sqlite3 \
 *       examples/http-serve.c examples/http-serve-cchan.c \
 *       examples/http-serve-workers.cy \
 *       examples/http_crud/main.cy examples/http_crud/items.cy -eg -- test
 *
 * Blocking / fibers / workers:
 *
 *   … -eg -- --port=8080
 *   … -eg -- fibers --port=8080
 *   … -eg -- workers --workers=4 --port=8080
 */

#include "httpserve.h"
#include <stdio.h>

extern void items_boot(void);

Response* app_handle(Request* req) {
    return route_dispatch(req);
}

/* ── self-test ────────────────────────────────────────────────────────── */

static void show(const char* label, Request* req) {
    owned Response* r = route_dispatch(req);
    int st = r->status;
    String body = r->body;
    if ((char*)body == NULL) body = "";
    printf(f"  {label} → {st}  {body}\n");
}

static int selftest(void) {
    printf("routes:\n");
    for (RouteReg** p = __start_cyreg_routes; p < __stop_cyreg_routes; p++)
        printf(f"  {(*p)->method} {(*p)->path}\n");

    printf("\nCRUD:\n");
    {
        owned Request* q = new Request("GET", "/api/items", "", "");
        show("GET  /api/items", q);
    }
    {
        owned Request* q = new Request("POST", "/api/items", "",
            "{\"name\":\"Sprocket\",\"qty\":7,\"note\":\"bin C\"}");
        show("POST /api/items", q);
    }
    {
        owned Request* q = new Request("GET", "/api/items/1", "", "");
        show("GET  /api/items/1", q);
    }
    {
        owned Request* q = new Request("PUT", "/api/items/1", "", "{\"qty\":42}");
        show("PUT  /api/items/1", q);
    }
    {
        owned Request* q = new Request("DELETE", "/api/items/2", "", "");
        show("DELETE /api/items/2", q);
    }
    {
        owned Request* q = new Request("GET", "/api/items/999", "", "");
        show("GET  /api/items/999", q);
    }
    {
        owned Request* q = new Request("GET", "/api/items", "q=Widget", "");
        show("GET  /api/items?q=Widget", q);
    }

    printf("\nhttp_crud OK\n");
    return 0;
}

/* ── CLI ──────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    int fibers = 0, workers = 0, test = 0, port = 8080, nworkers = 4;

    /* char** isn't a for-in collection yet (no Count/Get / array size), so
       bind each argv[i] as a String and use .equals / .starts_with. */
    for (int i = 1; i < argc; i++) {
        String s = argv[i];
        if (s.equals("--")) continue;   /* classyc -eg -- separator */
        if (s.equals("test")    || s.equals("--test"))    test = 1;
        else if (s.equals("fibers")  || s.equals("--fibers"))  fibers = 1;
        else if (s.equals("workers") || s.equals("--workers")) workers = 1;
        else if (s.starts_with("--port="))
            port = atoi((char*) s.substr(7, 16));
        else if (s.starts_with("--workers=")) {
            workers = 1;
            nworkers = atoi((char*) s.substr(10, 8));
            if (nworkers < 1) nworkers = 1;
        }
    }

    items_boot();

    if (test) return selftest();

    String mode = workers ? "workers" : (fibers ? "fibers" : "blocking");
    printf("════════════════════════════════════════\n");
    printf("  http_crud · attribute routes\n");
    if (workers)
        printf(f"  {mode} · :{port} · {nworkers} OS threads\n");
    else
        printf(f"  {mode} · :{port}\n");
    for (RouteReg** p = __start_cyreg_routes; p < __stop_cyreg_routes; p++)
        printf(f"    {(*p)->method} {(*p)->path}\n");
    printf("════════════════════════════════════════\n");
    fflush(stdout);

    if (workers) return serve_workers(port, nworkers);
    if (fibers)  return serve_workers_cchan(port, 1);
    return serve(port);
}
