/* Test 119: Variadic functions and macros with ClassyC types */
#include <stdio.h>
#include <stdarg.h>

// Variadic function taking String arguments
void printStrings(int count, ...) {
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        String s = va_arg(args, String);
        printf("  arg%d: %s\n", i, s);
    }
    va_end(args);
}

// Variadic macro
#define LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

int main() {
    String a = "hello";
    String b = "world";
    String c = "classyc";

    printStrings(3, a, b, c);

    LOG("variadic macro
    LOG("simple log");
    LOG("with int: %d", 42);
    LOG("with string: %s", "test");
    LOG("multiple: %d, %s, %f", 1, "two", 3.0);

    // String in variadic context
    String dynamic = f"dynamic {42}";
    LOG("dynamic: %s", dynamic);

    return 0;
}
