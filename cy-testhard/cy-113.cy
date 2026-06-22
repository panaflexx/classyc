/* Test 113: Exception safety with defer and resource cleanup */
#include <stdio.h>

class Resource {
    String name;
    Resource(String n) { this.name = n; printf("acquire %s\n", this.name); }
    ~Resource() { printf("release %s\n", this.name); }
    void use() { printf("using %s\n", this.name); }
};

void riskyOperation(int fail) {
    Resource r1("resource1");
    defer delete new Resource("deferred1");

    if (fail) {
        throw(RuntimeException, "operation failed");
    }

    Resource r2("resource2");
    defer delete new Resource("deferred2");

    r1.use();
    r2.use();
}

int main() {
    printf("=== Success case ===\n");
    try {
        riskyOperation(0);
    } catch (Exception e) {
        printf("caught: %s\n", e.msg);
    }

    printf("\n=== Failure case ===\n");
    try {
        riskyOperation(1);
    } catch (Exception e) {
        printf("caught: %s\n", e.msg);
    }

    printf("\n=== Nested try with defer ===\n");
    try {
        defer printf("outer defer\n");
        try {
            defer printf("inner defer\n");
            throw(RuntimeException, "inner throw");
        } catch (RuntimeException e) {
            printf("caught inner: %s\n", e.msg);
        }
        printf("after inner catch\n");
    } catch (Exception e) {
        printf("caught outer: %s\n", e.msg);
    }

    return 0;
}
