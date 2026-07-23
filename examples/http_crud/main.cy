/* http_crud — tiny inventory API, attribute-routed.
 *
 * Controllers (items.cy) register themselves with ROUTE() / [[registry]].
 * This file is only the entry point: dispatch + boot + server mode.
 *
 * Self-test (no sockets):
 *
 *   ./bin/classyc -I include -l sqlite3 \
 *       examples/http-serve.c \
 *       examples/http_crud/main.cy examples/http_crud/items.cy -eg -- test
 *
 * Blocking server:
 *
 *   ./bin/classyc -I include -l sqlite3 \
 *       examples/http-serve.c \
 *       examples/http_crud/main.cy examples/http_crud/items.cy -eg
 *
 * Fiber server (concurrent clients, one OS thread):
 *
 *   ./bin/classyc -I include -I ext/ccchan -l sqlite3 \
 *       examples/http-serve-fibers.c \
 *       examples/http_crud/main.cy examples/http_crud/items.cy -eg -- fibers
 *
 * curl:
 *   curl -s http://127.0.0.1:8080/api/items
 *   curl -s http://127.0.0.1:8080/api/items/1
 *   curl -s -X POST -d '{"name":"Sprocket","qty":5}' http://127.0.0.1:8080/api/items
 *   curl -s -X PUT  -d '{"qty":99}' http://127.0.0.1:8080/api/items/1
 *   curl -s -X DELETE http://127.0.0.1:8080/api/items/2
 */

#include "httpserve.h"
#include <stdio.h>
#include <string.h>

extern void items_boot(void);

Response* app_handle(Request* req) {
    return route_dispatch(req);
}

static void show(const char* label, Request* req) {
    owned Response* r = route_dispatch(req);
    printf("  %-22s → %d  %s\n", label, r->status,
           r->body != NULL ? (char*)r->body : "");
}

static int selftest(void) {
    printf("routes:\n");
    for (RouteReg** p = __start_cyreg_routes; p < __stop_cyreg_routes; p++)
        printf("  %-6s %s\n", (*p)->method, (*p)->path);

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

int main(int argc, char** argv) {
    int fibers = 0, test = 0, port = 8080;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "fibers") || !strcmp(argv[i], "--fibers")) fibers = 1;
        else if (!strcmp(argv[i], "test") || !strcmp(argv[i], "--test")) test = 1;
        else if (!strncmp(argv[i], "--port=", 7)) port = atoi(argv[i] + 7);
    }

    items_boot();

    if (test) return selftest();

    printf("════════════════════════════════════════\n");
    printf("  http_crud · attribute routes\n");
    printf("  %s · :%d\n", fibers ? "fibers" : "blocking", port);
    for (RouteReg** p = __start_cyreg_routes; p < __stop_cyreg_routes; p++)
        printf("    %-6s %s\n", (*p)->method, (*p)->path);
    printf("════════════════════════════════════════\n");
    fflush(stdout);

    return fibers ? serve_fibers(port) : serve(port);
}
