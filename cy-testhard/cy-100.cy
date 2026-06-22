/* Test 100: Dict with complex nested structures and json round-trip */
#include <stdio.h>

int main() {
    dict config = {
        "database": {
            "host": "localhost",
            "ports": [5432, 5433, 5434],
            "ssl": 1,
            "options": {
                "pool_size": 10,
                "timeout": 30.5
            }
        },
        "servers": [
            { "name": "web1", "ip": "10.0.0.1" },
            { "name": "web2", "ip": "10.0.0.2" }
        ],
        "debug": 1
    };

    // Access nested
    printf("host: %s\n", config.database.host);
    printf("port0: %d\n", config.database.ports[0]);
    printf("pool: %d\n", config.database.options.pool_size);
    printf("server0: %s\n", config.servers[0].name);

    // Modify
    config.database.host = "remote";
    config.servers[1].ip = "10.0.0.3";
    config.new_key = "added";

    // JSON round-trip
    String json_str = json(config);
    printf("json: %s\n", json_str);

    dict parsed = json(json_str);
    printf("parsed host: %s\n", parsed.database.host);
    printf("parsed new_key: %s\n", parsed.new_key);

    return 0;
}
