/* test-any-arena.c — Phase 2b: arena-managed Any<I> handles.
 *
 * Every `any<I>(...)` handle is registered in a scope-bound object arena.
 * When the scope that constructed it exits *and the handle was never
 * retained anywhere else*, the compiler automatically destroys it — running
 * ~__Any_I, which frees the wrapped concrete object via its dtor slot.
 *
 * That automatic reclamation stops the moment a handle is stored into a
 * collection (`list->Add(any<I>(...))`): the collection may outlive this
 * function (or even just this loop iteration), so auto-freeing at scope exit
 * would risk freeing something the collection still points at. From that
 * point the handle is an ordinary owned pointer, exactly like
 * `List<Track*>`: the collection needs `.owns()` so its destructor deletes
 * every element (see below), or each handle needs an explicit `delete`. See
 * cy-validate/SHORTCOMINGS.md ("Any<I>* handles retained in a collection")
 * and val-059-any-collection-escape.cy.
 *
 * Explicit heap management (new/delete) remains available for things the
 * dev owns directly (here, the List container itself).
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
    /* The List frees only its own backing buffer (see ~List above), and a
       handle stored into it is no longer auto-reclaimed at scope exit (see
       the header comment) -- so we own deleting each handle by hand, same as
       any other `List<Track*>`-shaped pointer collection without `.owns()`. */
    List<Any<View>*>* widgets = new List<Any<View>*>();
    defer delete widgets;

    widgets->Add(any<View>(new Button("OK")));
    widgets->Add(any<View>(new Text("hello")));
    widgets->Add(any<View>(new Button("Cancel")));

    printf("-- render (count = %d) --\n", widgets->Count());
    for (auto v in widgets) v->render();

    printf("-- leaving scope: deleting handles by hand (LIFO) --\n");
    for (int i = widgets->Count() - 1; i >= 0; i--) delete widgets->Get(i);
    /* on return: ~Cancel, ~hello, ~OK already ran above, then `defer delete
       widgets` frees just the List's own backing buffer. */
}

int main() {
    printf("=== arena-managed Any<View> (Phase 2b) ===\n\n");
    build_and_render();
    printf("\nback in main — everything already cleaned up.\n");
    return 0;
}
