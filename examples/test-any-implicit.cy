/* test-any-implicit.c — Phase 2c: implicit C* -> Any<I>* coercion.
 *
 * When a concrete C* is placed where an erased handle Any<I>* is expected and C
 * structurally satisfies I, the compiler inserts the any<I>(...) wrap for you.
 * So the verbose, repeated cast goes away:
 *
 *     widgets->Add(any<View>(new Button("OK")));   // explicit (still works)
 *     widgets->Add(new Button("OK"));              // implicit  (this file)
 *
 * It also makes the declarative brace-initializer read naturally:
 *
 *     new List<Any<View>*>{ new Button("Save"), new Text("world") };
 *
 * Handles remain arena-managed: no manual delete of the elements is needed.
 */
#include <stdio.h>
#include <stdlib.h>

class List<T> {
    T*  data;
    int length;
    int capacity;
    List() { this.length = 0; this.capacity = 4; this.data = (T*) malloc(sizeof(T) * this.capacity); }
    ~List() { if (this.data) free((void*) this.data); }
    int Count() { return this.length; }
    T Get(int i) { return this.data[i]; }
    void Add(T item) {
        if (this.length >= this.capacity) { this.capacity *= 2; this.data = (T*) realloc(this.data, sizeof(T) * this.capacity); }
        this.data[this.length] = item;
        this.length += 1;
    }
};

interface View { void render(); }

class Button impl View {
    String label;
    Button(String label) { this.label = label; }
    void render() { printf("  [Button] %s\n", this.label); }
    ~Button() { printf("  ~Button(%s)\n", this.label); }
};

class Text {
    String s;
    Text(String s) { this.s = s; }
    void render() { printf("  [Text] %s\n", this.s); }
    ~Text() { printf("  ~Text(%s)\n", this.s); }
};

/* A plain helper over the erased interface — no special magic, just generics. */
void render_all(List<Any<View>*>* items) {
    for (auto v in items) v->render();
}

int main() {
    printf("=== implicit any<View> coercion (Phase 2c) ===\n\n");

    /* (1) method-argument coercion — no any<View>(...) at the call site */
    printf("-- a: list->Add(new Button(...)) --\n");
    List<Any<View>*>* a = new List<Any<View>*>();
    defer delete a;
    a->Add(new Button("OK"));
    a->Add(new Text("hello"));
    a->Add(new Button("Cancel"));
    render_all(a);

    /* (2) declarative brace-initializer — each element auto-wrapped */
    printf("\n-- b: new List<Any<View>*>{ ... } --\n");
    List<Any<View>*>* b = new List<Any<View>*>{
        new Button("Save"),
        new Text("world"),
        new Button("Load")
    };
    defer delete b;
    render_all(b);

    printf("\n-- leaving main: arena reclaims all handles --\n");
    return 0;
}
