/* test-any-arena.c — Phase 2b: arena-managed Any<I> handles.
 *
 * "Collections are arena managed" (like String / dict): every `any<I>(...)`
 * handle is registered in a scope-bound object arena.  When the scope that
 * constructed it exits, the compiler automatically destroys each handle —
 * running ~__Any_I, which frees the wrapped concrete object via its dtor slot.
 *
 * So the developer does NOT delete the handles or the concrete objects placed
 * into the list; the compiler reclaims them when they go out of scope.  Explicit
 * heap management (new/delete) remains available for things the dev owns
 * directly (here, the List container itself).
 */
#include <stdio.h>
#include <stdlib.h>

/* A generic List<T>.  Note ~List does NOT delete its elements: Any<I> handles
   are owned by the object arena, not by the container. */
class List<T> {
    T*  data;
    int length;
    int capacity;
    List() { this.length = 0; this.capacity = 4; this.data = (T*) malloc(sizeof(T) * this.capacity); }
    ~List() { if (this.data) free((void*) this.data); }   /* frees backing store only */
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

void build_and_render() {
    /* The List is the only thing we manage by hand. */
    List<Any<View>*>* widgets = new List<Any<View>*>();
    defer delete widgets;

    /* No `new`/`delete` bookkeeping for the elements: each handle (and the
       Button/Text it wraps) is reclaimed automatically at function scope exit. */
    widgets->Add(any<View>(new Button("OK")));
    widgets->Add(any<View>(new Text("hello")));
    widgets->Add(any<View>(new Button("Cancel")));

    printf("-- render (count = %d) --\n", widgets->Count());
    for (auto v in widgets) v->render();

    printf("-- leaving scope: arena reclaims handles (LIFO) --\n");
    /* on return: ~Cancel, ~hello, ~OK run automatically, then `defer delete widgets` */
}

int main() {
    printf("=== arena-managed Any<View> (Phase 2b) ===\n\n");
    build_and_render();
    printf("\nback in main — everything already cleaned up.\n");
    return 0;
}
