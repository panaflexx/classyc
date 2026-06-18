/* Test 053: Dict with nested for-in */
#include <stdio.h>

int main() {
    dict config = {
        "server": {
            "host": "localhost",
            "port": 8080
        }
    };
    printf("nested dict for-in: ");
    for (auto k in config.server) {
        printf("%s ", k);
    }
    printf("\n");
    return 0;
}