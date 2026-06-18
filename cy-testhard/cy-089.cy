/* Test 089: Dict with complex nested for-in */
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
        }
    };
    printf("complex nested for-in: ");
    for (auto k in config.server.ssl) {
        printf("%s ", k);
    }
    printf("\n");
    return 0;
}