/* Test 029: Complex nested dict */
#include <stdio.h>

int main() {
    dict config = {
        "server": {
            "host": "localhost",
            "port": 8080,
            "ssl": {
                "enabled": 1,
                "cert": "server.crt"
            }
        },
        "database": {
            "url": "postgres://localhost/mydb"
        }
    };
    printf("complex nested: %s:%d\n", config.server.host, config.server.port);
    return 0;
}