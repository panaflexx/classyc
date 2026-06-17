/* Test 066: Dict with complex nested structure */
#include <stdio.h>

int main() {
    dict config = {
        "database": {
            "connection": {
                "host": "localhost",
                "port": 5432,
                "credentials": {
                    "username": "admin",
                    "password": "secret"
                }
            }
        }
    };
    printf("deep nested: %s:%d\n", config.database.connection.host, config.database.connection.port);
    return 0;
}