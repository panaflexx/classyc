/* val-012-interfaces-any.cy — validates interface/impl, structural conformance,
 * and Any<I> type erasure in a heterogeneous List<Any<I>*>.
 *
 * - Button opts in with `impl View` (conformance checked at the class).
 * - Text and Spacer conform purely STRUCTURALLY (they just have render()).
 * - any<View>(x) erases the concrete type; for-in dispatches to the right one.
 * - deleting an Any<View>* handle recursively runs the wrapped destructor.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-012-interfaces-any.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int rendered_buttons = 0, rendered_texts = 0, rendered_spacers = 0;
int destroyed = 0;

interface View {
    void render();
}

class Button impl View {           /* opt-in conformance */
    String label;
    Button(String label) { this.label = label; }
    void render() { rendered_buttons++; }
    ~Button() { destroyed++; }
};

class Text {                        /* structural conformance (no impl) */
    String s;
    Text(String s) { this.s = s; }
    void render() { rendered_texts++; }
    ~Text() { destroyed++; }
};

class Spacer {                      /* structural conformance (no impl) */
    Spacer() {}
    void render() { rendered_spacers++; }
    ~Spacer() { destroyed++; }
};

int main() {
    printf("=== val-012 interfaces + Any<I> ===\n\n");

    List<Any<View>*>* widgets = new List<Any<View>*>();
    widgets->Add(any<View>(new Button("OK")));
    widgets->Add(any<View>(new Text("hello")));
    widgets->Add(any<View>(new Spacer()));
    widgets->Add(any<View>(new Button("Cancel")));

    check(widgets->Count() == 4, "heterogeneous List<Any<View>*> holds 4");

    /* erased dispatch: each render() lands on the correct concrete type */
    for (auto v in widgets) v->render();
    check(rendered_buttons == 2, "Any dispatch: 2 Buttons rendered (impl View)");
    check(rendered_texts == 1,   "Any dispatch: 1 Text rendered (structural)");
    check(rendered_spacers == 1, "Any dispatch: 1 Spacer rendered (structural)");

    /* recursive RAII: deleting a handle runs the wrapped object's destructor */
    for (auto v in widgets) delete v;
    check(destroyed == 4, "delete Any<View>* recursively destroys wrapped objects");

    delete widgets;   /* frees the backing store (handles already deleted) */

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
