/* Test 007: Nested dict */
#include <stdio.h>

int main() {
    dict d = {
        "server": {
            "host": "localhost",
            "port": 8080
        },
        "debug": 1
    };
    printf("nested: %s:%d\n", d.server.host, d.server.port);
    return 0;
}