/* test-any.c — Phase 2: Any<I> erased handle + any<I>(x) intrinsic.
 *
 * Exercises the compiler-synthesized erased handle and construction intrinsic:
 *   - `interface View { void render(); }`               (Phase 1 contract)
 *   - `Any<View>`                                        (synthesized __Any_View)
 *   - `any<View>(new C(...))`                            (structural wrap, no impl)
 *   - heterogeneous `List<Any<View>*>`                   (rides generics, no new
 *                                                         container code)
 *   - `for (auto v in list) v->render()`                 (Count()/Get() protocol)
 *   - `delete list`                                      (recursive RAII: ~List →
 *                                                         ~__Any_View → ~C)
 *
 * Button opts in with `impl View`; Text and Spacer conform purely structurally
 * (they just have render()).  All three slot into the same List<Any<View>*>.
 */
#include <stdio.h>
#include <stdlib.h>

/* ── A minimal generic List<T> (the heterogeneous container) ─────────────── */
class List<T> {
    T*  data;
    int length;
    int capacity;

    List() {
        this.length = 0;
        this.capacity = 4;
        this.data = (T*) malloc(sizeof(T) * this.capacity);
    }
    ~List() {
        /* delete each owned handle, then free the backing store */
        for (int i = 0; i < this.length; i++) delete this.data[i];
        if (this.data) free((void*) this.data);
    }
    int Count() { return this.length; }
    T Get(int i) { return this.data[i]; }
    void Add(T item) {
        if (this.length >= this.capacity) {
            this.capacity *= 2;
            this.data = (T*) realloc(this.data, sizeof(T) * this.capacity);
        }
        this.data[this.length] = item;
        this.length += 1;
    }
};

/* ── The interface contract ──────────────────────────────────────────────── */
interface View {
    void render();
}

/* ── Concrete views ──────────────────────────────────────────────────────── */
class Button impl View {           /* opt-in: conformance checked here */
    String label;
    Button(String label) { this.label = label; }
    void render() { printf("  [Button] %s\n", this.label); }
    ~Button() { printf("  ~Button(%s)\n", this.label); }
};

class Text {                       /* no impl — conforms structurally */
    String s;
    Text(String s) { this.s = s; }
    void render() { printf("  [Text] %s\n", this.s); }
    ~Text() { printf("  ~Text(%s)\n", this.s); }
};

class Spacer {                     /* no impl — conforms structurally */
    Spacer() {}
    void render() { printf("  [Spacer]\n"); }
    ~Spacer() { printf("  ~Spacer\n"); }
};

int main() {
    printf("=== Any<View> heterogeneous collection (Phase 2) ===\n\n");

    List<Any<View>*>* widgets = new List<Any<View>*>();
    widgets->Add(any<View>(new Button("OK")));
    widgets->Add(any<View>(new Text("hello")));
    widgets->Add(any<View>(new Spacer()));
    widgets->Add(any<View>(new Button("Cancel")));

    printf("-- render via for-in (count = %d) --\n", widgets->Count());
    for (auto v in widgets) v->render();

    printf("\n-- delete widgets (destructor ordering) --\n");
    delete widgets;

    printf("\nDone.\n");
    return 0;
}
