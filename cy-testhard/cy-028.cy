/* Test 028: Dict arena */
#include <stdio.h>

int main() {
    String arena = String.checkpoint();
    defer String.release_to(arena);
    
    dict d = { "name": "test", "value": 42 };
    printf("arena dict: %s = %d\n", d.name, d.value);
    return 0;
}