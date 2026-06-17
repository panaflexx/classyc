/* Test 035: Dict with complex values */
#include <stdio.h>

int main() {
    dict d = {
        "name": "test",
        "numbers": [1, 2, 3, 4],
        "config": {
            "debug": 1,
            "timeout": 30.5
        }
    };
    printf("complex dict: %s, %d\n", d.name, d.config.timeout);
    return 0;
}