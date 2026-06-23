// test-arena-keywords.cy - probe how the ownership analyzer handles the
// arena-keyword patterns (detach / unowned / attach + class field stores).
//
//   bin/classyc -I include -fownership-report examples/test-arena-keywords.cy

#include <stdio.h>
#include <stdlib.h>

class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() {}
};

// (1) return detach <String expression>
String build_label(int i) {
    return detach (String)"x#" + i;
}

// (2) return detach new T(...)
Box* spawn(int v) {
    return detach new Box(v);
}

// (3) detach into a field store
class Request {
    String method;
    Request(String m) { this.method = detach m.trim().upper(); }
};

// (4) unowned + defer delete + attach stub
void handle() {
    unowned Box* held = new Box(42);
    defer delete held;

    // attach external_ptr;  // attach is a parsed stub; demonstrating it
                              // would need a real pointer named `external_ptr`
}

// Caller of spawn - should this be tracked as an acquire?
void caller_of_spawn() {
    Box* b = spawn(99);
    // (no delete) - is this a leak?
}

int main() {
    String s = build_label(1);
    printf("%s\n", s);

    Box* b = spawn(7);
    delete b;

    Request* r = new Request("  GET  ");
    delete r;

    handle();
    caller_of_spawn();
    return 0;
}
