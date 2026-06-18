/* Test 070: Dict with JSON round-trip */
#include <stdio.h>

int main() {
    dict original = { "name": "test", "value": 42 };
    char *json_str = json(original);
    dict parsed = json(json_str);
    printf("json round-trip: %s = %d\n", parsed.name, parsed.value);
    return 0;
}