/* Test 050: Dict with array of dicts */
#include <stdio.h>

int main() {
    dict config = {
        "servers": [
            { "host": "server1", "port": 8080 },
            { "host": "server2", "port": 8081 }
        ]
    };
    printf("array of dicts: %s:%d\n", config.servers[0].host, config.servers[0].port);
    return 0;
}