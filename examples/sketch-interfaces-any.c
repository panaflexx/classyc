/* sketch-interfaces-any.c
 *
 * DESIGN SKETCH (no implementation yet) — the recommended evolution of the
 * `sketch-kitchen-sink-ui.c` idea, now WITH the SwiftUI-style declarative
 * `View { ... }` builder as first-class "parsed magic".
 *
 * Goal: get the kitchen-sink's heterogeneous-collection + declarative-UI
 * ergonomics WITHOUT the three features that fight ClassyC's design:
 *
 *     ✗ C++-style free-function templates   (template<Concrete> ...)
 *     ✗ user-written partial specialization (class Any<Drawable> { ... })
 *     ✗ capture lambdas                     ([](void* p){ ... })
 *
 * We add exactly THREE small, zero-runtime-cost language features, each a
 * natural extension of machinery ClassyC already has:
 *
 *   1. `interface` / `impl`  — a NAMED, compile-time-checked duck-type.  A
 *      first-class version of today's structural protocol resolver
 *      (find_class_protocol_method, which already does Count/Get/Add/Set).
 *
 *   2. `Any<I>` + `any<I>(x)` — a COMPILER-SYNTHESIZED erased handle generated
 *      from an interface I.  No hand-written struct, thunks, or factory.
 *
 *   3. A `View` builder block — `Container { childA  childB  ... }` is sugar
 *      that the PARSER recognizes and lowers to building a List<Any<View>*>
 *      and calling the container.  This is the "parsed magic" you want.
 *
 * Everything else (generics monomorphization, the for-in protocol, statement
 * expressions, non-capturing lambdas, new/delete/defer) is reused unchanged.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ═══════════════════════════════════════════════════════════════════════════
   SECTION A — interface / impl   (feature #1)
   ═══════════════════════════════════════════════════════════════════════════

   COMPILER NOTE — STRUCTURAL, like List's Count/Get/Add (impl is OPTIONAL)
   -----------------------------------------------------------------------
   `interface` records a set of instance-method signatures in a name-keyed
   table.  It emits NO runtime type, NO vtable, NO global data and changes NO
   object layout.

   Conformance is STRUCTURAL and IMPLICIT — exactly the duck-typing the compiler
   already does for List today (find_class_protocol_method matches Count()/
   Get(int)/Add(item) by name+signature, with no declaration anywhere).  ANY
   class that defines a matching `render()` IS a View; it does not need to say
   so.  This is the Go interface model, not the Rust/Swift nominal one.

   `impl View` is therefore OPTIONAL.  Writing it does one thing: it asks the
   compiler to verify conformance HERE and now, turning a late use-site error
   into an early, precise one ("class Button does not satisfy View: missing
   render()").  It adds no fields, no pointer, no layout change, and does NOT
   gate usage — a class without `impl` conforms just the same.

   Why name the interface at all, if matching is structural?  Because ERASURE
   needs a fixed method set (see SECTION B): `Any<View>` has one slot per method
   of View, so the compiler must know View's shape by name.  Direct/inline use
   (for-in, calling render() on a concrete value) needs no interface at all and
   stays exactly as fast as today.
*/
interface View {
    void render();
}

interface Clickable {
    bool hitTest(int x, int y);
    void onClick(int x, int y);
}

/* ═══════════════════════════════════════════════════════════════════════════
   SECTION B — Any<I> : compiler-synthesized erased handle   (feature #2)
   ═══════════════════════════════════════════════════════════════════════════

   COMPILER NOTE — Any<View>
   -------------------------
   `Any<View>` is NOT user-written.  The first time the compiler sees it, it
   SYNTHESIZES once (because it already knows the View contract):

       struct __Any_View {
           void* data;
           void (*dtor)(void*);
           void (*render_fn)(void*);     // one slot per interface method
       };
       void __Any_View_render(struct __Any_View* h) {        // accessor
           if (h->render_fn) h->render_fn(h->data);
       }
       // ~__Any_View calls h->dtor(h->data)  → owns its concrete object.

   COMPILER NOTE — any<View>(concrete*)
   ------------------------------------
   `any<View>(new Button(...))` is an intrinsic that replaces the old
   `make_drawable<Concrete>` template.  For each distinct concrete type C
   (which must `impl View`) it MONOMORPHIZES — reusing the SAME machinery as
   List<int> — a couple of static thunks and fills the handle's slots:

       static void __thunk_dtor_Button  (void* p) { delete (Button*)p; }
       static void __thunk_render_Button(void* p) { ((Button*)p)->render(); }

   No capture lambdas: each thunk captures only a TYPE, resolved statically.
   This keeps ClassyC's non-capturing-lambda rule intact.

   Any<View> rides existing generics + for-in:
     • List<Any<View>*>           — ordinary generic instantiation,
     • for (auto v in list) ...   — existing Count()/Get(int) protocol,
     • delete / defer             — recursive RAII via synthesized ~Any.
*/

/* ═══════════════════════════════════════════════════════════════════════════
   SECTION C — the View builder block   (feature #3, the "parsed magic")
   ═══════════════════════════════════════════════════════════════════════════

   USER-FACING SHAPE
   -----------------
       Container { child  child  child }

   where each `child` is one of:
       •  TypeName(args)          — a class that impl View   (leaf)
       •  Container { ... }       — a nested builder         (recursion)
       •  <expr of type Any<View>*>                          (pass-through)
       •  if (cond) child         — conditional inclusion    (result-builder)
       •  for (auto x in seq) child   — repeated inclusion   (result-builder)
       •  let name = expr;        — local binding visible to later children

   A container opts in to this syntax by declaring its child parameter with the
   soft keyword `views` (sugar for `List<Any<View>*>*`).  Declaring such a param
   REGISTERS the container's name as "builder-callable" in the parser, exactly
   like generic class names and typedef names are registered during parsing.

   ── How the PARSER disambiguates `{` ───────────────────────────────────────
   A `{` immediately following a builder-callable (a bare builder name, or a
   call to one) in EXPRESSION position is a view-builder block — never a
   compound statement (we're in an expression), never a dict/array literal
   (those need `key:`/`,`), never `new T{...}` brace-init (that attaches to
   `new`).  If the name is not a registered builder, `{` keeps its normal
   meaning.  Net: zero ambiguity with existing constructs.

   ── New AST node ────────────────────────────────────────────────────────────
   The block parses to `N_VIEW_BUILDER` { callee, item_list }, where each item
   is a child expression or a control-flow wrapper (N_IF / N_FORIN / N_LET).

   ── CHECK ────────────────────────────────────────────────────────────────────
   For each child item the checker normalizes it to an `Any<View>*`.  Note the
   View test is STRUCTURAL (does the class have render()?) — NOT "did it write
   impl View?".  Same rule List uses for Count/Get/Add:
       •  TypeName(args)  where TypeName structurally has View's methods
                                                    →  any<View>(new TypeName(args))
       •  already Any<View>*                        →  used as-is
       •  concrete value/ptr structurally a View    →  any<View>(...)
       •  anything else                             →  error "not a View"
   Control-flow items are checked recursively over their inner child.
   The callee must accept a `List<Any<View>*>*` (its `views` param).

   ── GEN (desugaring) ────────────────────────────────────────────────────────
   `Container { a  b  c }` lowers to a statement-expression:

       ({
           List<Any<View>*>* __b = new List<Any<View>*>();
           __b->Add(<normalized a>);
           __b->Add(<normalized b>);
           __b->Add(<normalized c>);
           Container(__b);                  // returns Container* / Any<View>*
       })

   `if`/`for` items emit guarded / looped `__b->Add(...)` exactly as written —
   that is the whole "result builder" mechanism, done with constructs ClassyC
   already lowers.  No new runtime support, no allocation beyond the List the
   user would have written by hand.
*/

/* ───────────────────────── Leaf + container View types ─────────────────────

   These show BOTH styles deliberately:
     • Button / VStack / HStack write `impl View` — opt-in early checking.
     • Text / Spacer do NOT — they conform purely by having render(), exactly
       like a class conforms to List's iteration protocol just by having
       Count()/Get(int).  Both kinds are equally usable as Views.            */

class Button impl View, Clickable {                /* opt-in: checked here */
    int x, y, w, h;
    String label;
    bool pressed;

    Button(String label) {                         /* convenience ctor for the builder */
        this.x = 0; this.y = 0; this.w = 120; this.h = 32;
        this.label = label; this.pressed = false;
    }
    Button(int x, int y, int w, int h, String label) {
        this.x = x; this.y = y; this.w = w; this.h = h;
        this.label = label; this.pressed = false;
    }

    void render() {
        printf("  [Button] %s%s\n", this.label, this.pressed ? " (pressed)" : "");
    }
    bool hitTest(int px, int py) {
        return px >= this.x && px < this.x + this.w &&
               py >= this.y && py < this.y + this.h;
    }
    void onClick(int px, int py) {
        this.pressed = true;
        printf("  >>> Button '%s' clicked!\n", this.label);
    }
}

/* No `impl View` — but it has render(), so it IS a View (pure duck typing,
   just like a class with Count()/Get() is iterable without any declaration). */
class Text {
    String s;
    Text(String s) { this.s = s; }
    void render() { printf("  [Text] %s\n", this.s); }
}

/* Also no `impl` — conforms structurally. */
class Spacer {
    Spacer() {}
    void render() { printf("  [Spacer]\n"); }
}

/* A container is just a View that owns a list of child Views.  The `views`
   parameter is what makes `VStack { ... }` legal at call sites. */
class VStack impl View {
    List<Any<View>*>* kids;
    VStack(views kids) { this.kids = kids; }       /* `views` ≡ List<Any<View>*>* + builder opt-in */
    void render() {
        printf("  [VStack]\n");
        for (auto k in this.kids) k->render();
    }
    ~VStack() { if (this.kids) delete this.kids; }  /* deletes each Any → each concrete */
}

class HStack impl View {
    List<Any<View>*>* kids;
    HStack(views kids) { this.kids = kids; }
    void render() {
        printf("  [HStack]\n");
        for (auto k in this.kids) k->render();
    }
    ~HStack() { if (this.kids) delete this.kids; }
}

/* Plain functions over erased interfaces — no special magic, just generics. */
void render_all(List<Any<View>*>* items) {
    for (auto v in items) v->render();
}
void dispatch_click(List<Any<Clickable>*>* clickables, int x, int y) {
    for (auto c in clickables)
        if (c->hitTest(x, y)) { c->onClick(x, y); return; }
    printf("  (click at %d,%d missed all widgets)\n", x, y);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Demo
   ═══════════════════════════════════════════════════════════════════════════ */

int main() {
    printf("=== interface + Any<I> + View builder sketch ===\n\n");

    /* ---- The headline: SwiftUI-style declarative tree ----
       Note: no `new`, no `any<View>(...)`, no manual List/Add.  The builder
       block auto-constructs each leaf (TypeName(args) → any<View>(new ...))
       and auto-nests child builders.  Text and Spacer have NO `impl View`,
       yet they slot in next to Button/HStack purely because they have
       render() — structural duck typing, same as List's protocol. */
    printf("-- Declarative View tree --\n");

    bool admin = true;
    String menu[] = {"Home", "Docs", "About"};

    Any<View>* ui =
        VStack {
            Text("Dashboard")
            HStack {
                Button("Save")
                Button("Load")
            }
            Spacer()
            for (auto m in menu) Button(m)     /* result-builder loop */
            if (admin) Button("Admin")         /* result-builder branch */
        };
    defer delete ui;                            /* recursive cleanup: ~VStack → ~Any → concretes */

    ui->render();

    /* ---- The desugared equivalent works too (explicit, no magic) ---- */
    printf("\n-- Same tree, hand-built (what the magic lowers to) --\n");
    List<Any<View>*>* row = new List<Any<View>*>();
    row->Add(any<View>(new Button("OK")));
    row->Add(any<View>(new Text("hi")));
    render_all(row);
    delete row;

    /* ---- A second interface over the SAME concrete type ---- */
    printf("\n-- Click dispatch (Clickable) --\n");
    List<Any<Clickable>*>* clickables = new List<Any<Clickable>*>();
    defer delete clickables;
    clickables->Add(any<Clickable>(new Button(0,  0, 120, 32, "Save")));
    clickables->Add(any<Clickable>(new Button(0, 40, 120, 32, "Load")));

    dispatch_click(clickables, 10,  5);         /* hits Save */
    dispatch_click(clickables, 10, 50);         /* hits Load */
    dispatch_click(clickables, 999, 999);       /* misses    */

    printf("\nDone.\n");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
   IMPLEMENTATION ROADMAP (where each piece lands in classyc.c)
   ═══════════════════════════════════════════════════════════════════════════

   #1 interface / impl
      • lexer: add soft keywords `interface`, `impl`, `views` (match_soft_kw
        style, so they don't break existing identifiers).
      • parser: `interface` → record an N_INTERFACE { name, method sig list }
        in a new iface_tab (mirror tpname_tab).  `impl A, B` is OPTIONAL; when
        present it just stores a list of interfaces to verify on the class node.
      • check: ONE structural predicate does all the work — generalize
        find_class_protocol_method into
        `class_satisfies_interface_p(class_tag, iface)`.  It is called in two
        places:
          - eagerly, once per `impl` clause (early, precise diagnostics), and
          - lazily, at every erasure/builder site (so impl-less classes work).
        Conformance never depends on `impl` — only on having the methods.
      • gen: nothing — pure compile-time check, no emitted code.

   #2 Any<I> / any<I>(x)
      • check: when a type `Any<I>` is first referenced, look up interface I
        and SYNTHESIZE a class type `__Any_I` (data + dtor + one fn-ptr slot
        per method) plus an accessor method per slot and a ~__Any_I dtor.
        Register it like any other class so List<Any<I>*>, for-in, delete,
        and `->method()` all resolve through existing paths.
      • `any<I>(expr)` is a new intrinsic (handle it next to the seq-method /
        new_proto intrinsics): require expr to be `C*` with C impl I, then
        MONOMORPHIZE the dtor/forwarder thunks for C (reuse the generic
        specialization machinery + mangle_generic_name), and gen a heap
        __Any_I whose slots point at those thunks.  No closures involved.

   #3 View builder block
      • lexer/parser: `views param` registers the enclosing function/ctor name
        as builder-callable (a small builder_name set, mirror is_generic_class_p).
        In postfix-expression parsing, when a builder-callable is immediately
        followed by `{`, parse an N_VIEW_BUILDER { callee, items } instead of
        falling through to compound-statement/literal handling.  Each item is
        a child expression or an N_IF/N_FORIN/N_LET wrapper (reuse the existing
        statement parsers, but require each leaf to be an expression).
      • check: normalize every leaf item to Any<View>* —
          - N_CALL whose callee is a type T with T impl View → any<View>(new T(args))
          - expr already Any<View>*                         → as-is
          - concrete View value/ptr                          → any<View>(...)
          - else → error "not a View".
        Verify callee accepts List<Any<View>*>* (its `views` param).
      • gen: lower N_VIEW_BUILDER to the statement-expression shown in SECTION C
        (new List, guarded/looped Add per item, then call the callee).  All of
        these are nodes gen already emits, so there is no new runtime support.

   ── Build order (each independently testable) ──
      1. interface/impl + conformance check         (no Any, no builder)
      2. Any<I> synthesis + any<I>(x) intrinsic     (hand-built lists work)
      3. View builder block sugar on top of (1)+(2)

   ── Open questions to settle before coding ──
      • Builder opt-in spelling: `views kids` soft keyword vs. a `@builder`
        attribute vs. inferring from the `List<Any<View>*>*` param type.
      • Which interface a block erases to: hard-wire `View`, or make it
        `views<I>` / inferred from the param, so other builder protocols work.
      • Do we want a strict mode where erasing an impl-less class is a warning
        ("conforms structurally but never declared impl View"), for teams that
        prefer explicit intent?  Default stays implicit/structural like List.
      • Auto-`new` scope: only inside builder blocks, or anywhere a bare
        `TypeName(args)` appears in expression position?  (Keep it builder-only
        to avoid surprising existing code.)
      • Ownership: Any<I> always owns its data (delete in ~Any); do we also
        want a non-owning `ref<I>(x)` variant for borrowed views?
      • Multi-interface handles: ever need `Any<View, Clickable>`, or is one
        interface per handle enough?
*/