/* Test 121: Memory management edge cases with arenas */
#include <stdio.h>

int main() {
    // String arena checkpoint/release
    String arena1 = String.checkpoint();
    String s1 = "hello";
    String s2 = "world";
    String s3 = s1 + " " + s2;
    printf("arena1 strings: %s\n", s3);
    String.release_to(arena1);

    // Nested arenas
    String outer = String.checkpoint();
    String a = "outer";
    {
        String inner = String.checkpoint();
        String b = "inner";
        String c = a + b;
        printf("inner: %s\n", c);
        String.release_to(inner);
    }
    String d = "after inner";
    printf("outer after inner: %s\n", a + d);
    String.release_to(outer);

    // Dict arena
    dict arena_dict = new dict(1024);
    arena_dict.key1 = "value1";
    arena_dict.key2 = "value2";
    printf("arena dict: %s, %s\n", arena_dict.key1, arena_dict.key2);
    delete arena_dict;

    // Mixed arena and heap
    String heap_str = "heap";
    String arena2 = String.checkpoint();
    String mixed = heap_str + " " + "arena";
    printf("mixed: %s\n", mixed);
    String.release_to(arena2);
    printf("heap still valid: %s\n", heap_str);

    return 0;
}
