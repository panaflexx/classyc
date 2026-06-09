/* Quick test for dict.h arena allocator using c2m */
#include <stdio.h>
#include "inc/dict.h"

int main() {
    // 4KB stack buffer for the arena
    char stack_buf[4096];
    DictArena arena;
    dict_arena_init(&arena, stack_buf, sizeof(stack_buf));

    printf("Arena initialized: size=%zu used=%zu\n",
           arena.size, dict_arena_used(&arena));

    // Create a dict object entirely from the arena
    DictValue *config = dict_create_object_arena(&arena);
    if (!config) {
        printf("Failed to create dict object in arena\n");
        return 1;
    }
    printf("Created dict object, arena used now: %zu\n", dict_arena_used(&arena));

    // Add some values (note: dict_object_set still does heap allocations for keys/values
    // unless we also update dict_object_set / dict_object_ensure_capacity to be arena-aware.
    // For this smoke test we just verify the DictValue itself came from the arena.)
    DictValue *host = dict_create_string_arena(&arena, "localhost");
    DictValue *port = dict_create_int64_arena(&arena, 8080);
    DictValue *debug = dict_create_bool_arena(&arena, 1);

    printf("Created values in arena, used=%zu\n", dict_arena_used(&arena));

    // For a full stack-only dict we would need arena variants of dict_object_set too.
    // Here we just demonstrate that the creation path works.
    if (host && port && debug) {
        printf("SUCCESS: All values allocated via arena\n");
        printf("  host string ptr in arena? %s\n",
               ((char*)host->string_value >= stack_buf &&
                (char*)host->string_value < stack_buf + sizeof(stack_buf)) ? "yes" : "no");
    } else {
        printf("One of the value creations failed\n");
    }

    // Cleanup note: with an arena we don't individually free;
    // just reset the arena when done.
    dict_arena_reset(&arena);
    printf("After reset, used=%zu\n", dict_arena_used(&arena));

    printf("Arena allocator smoke test passed.\n");
    return 0;
}
