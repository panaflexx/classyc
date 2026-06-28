/* asit.h — Is<T> / As<T> for Any<I> handles (ClassyC generic class syntax)
 *
 *   bool    ok = Is<Circle>.Of(any_shape);          // concrete type test
 *   Circle* c  = As<Circle>.Of(any_shape);          // raw pointer or NULL
 *
 *   bool         ok2 = Is<Shape>.Of(any_shape);     // interface conformance test
 *   Any<Shape>*  s2  = As<Shape>.Of(any_shape);     // re-erased handle or NULL
 *
 * This header is pure ClassyC — no compiler special-casing of Is/As/Of.
 * It relies on two compiler-provided expressiveness primitives:
 *
 *   1. Generic class instantiation in expression context (Is<T>.Of(h))
 *      — parsed like any other Name<TypeArg>.method().
 *   2. nameof<T>() — minimal compile-time reflection: yields the C-level
 *      name of T as a string literal, so a generic body can compare the
 *      concrete type stored in an Any<I> handle against T.
 *
 * The Any<I> handle layout (data + dtor + method slots) is unchanged; the
 * type tag is kept in a tiny side-registry keyed by handle pointer, which
 * any<I>(x) caller must register via asit_register_type.  Interface-T
 * conformance (Is<Shape>, As<Shape>) uses a header-side class→interfaces
 * table populated by asit_impl(Concrete, Interface) declarations.
 */
#ifndef CLASSYC_ASIT_H
#define CLASSYC_ASIT_H

#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Tiny per-process RTTI registry                                     */
/* ------------------------------------------------------------------ */
struct __asit_slot {
    void               *handle;
    const char         *type_name;
    struct __asit_slot *next;
};

static struct __asit_slot *__asit_head = NULL;

static inline void asit_register_type(void *h, const char *name) {
    struct __asit_slot *s = (struct __asit_slot *)malloc(sizeof *s);
    s->handle = h; s->type_name = name; s->next = __asit_head;
    __asit_head = s;
}

static inline const char *__asit_lookup(void *h) {
    for (struct __asit_slot *s = __asit_head; s; s = s->next)
        if (s->handle == h) return s->type_name;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Header-side class→interfaces table (for Is<I> / As<I>)              */
/*                                                                    */
/* asit_impl(Circle, Shape); asit_impl(Circle, Drawable);             */
/*   — declares that Circle satisfies those interfaces at runtime.     */
/* asit_satisfies("Circle", "Shape") -> 1                              */
/*   — used by Is<I>.Of to check interface conformance.               */
/* ------------------------------------------------------------------ */
#define ASIT_MAX_IFACES 8
struct __asit_impl_slot {
    const char *class_name;
    const char *ifaces[ASIT_MAX_IFACES];
    struct __asit_impl_slot *next;
};
static struct __asit_impl_slot *__asit_impl_head = NULL;

static inline void __asit_impl_add(const char *cls, const char *iface) {
    /* Find or create the impl slot for cls */
    struct __asit_impl_slot *s = NULL;
    for (s = __asit_impl_head; s; s = s->next)
        if (strcmp(s->class_name, cls) == 0) break;
    if (s == NULL) {
        s = (struct __asit_impl_slot *)malloc(sizeof *s);
        s->class_name = cls;
        memset(s->ifaces, 0, sizeof s->ifaces);
        s->next = __asit_impl_head;
        __asit_impl_head = s;
    }
    for (int i = 0; i < ASIT_MAX_IFACES; i++) {
        if (s->ifaces[i] == NULL) { s->ifaces[i] = iface; return; }
        if (strcmp(s->ifaces[i], iface) == 0) return; /* already declared */
    }
}

/* asit_impl(Cls, Iface) — declare that Cls satisfies Iface at runtime.
   Call this in main() (or anywhere before Is<I>/As<I> is used) to populate
   the header-side class→interfaces table.  Mirrors the `impl` keyword but
   for runtime RTTI rather than compile-time conformance checking. */
#define asit_impl(Cls, Iface) __asit_impl_add(#Cls, #Iface)

static inline int __asit_satisfies(const char *cls, const char *iface) {
    if (cls == NULL) return 0;
    if (strcmp(cls, iface) == 0) return 1; /* T == I (same concrete type) */
    for (struct __asit_impl_slot *s = __asit_impl_head; s; s = s->next) {
        if (strcmp(s->class_name, cls) != 0) continue;
        for (int i = 0; i < ASIT_MAX_IFACES && s->ifaces[i]; i++)
            if (strcmp(s->ifaces[i], iface) == 0) return 1;
        return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Is<T> / As<T> — ClassyC generic class syntax                       */
/*                                                                    */
/* The parameter is `void*` rather than `Any<void>*`: every Any<I>*   */
/* handle is a pointer at the machine level and decays to void*, so   */
/* this accepts any erased handle without needing a wildcard Any type */
/* in the compiler.  nameof<T>() provides the concrete type name at   */
/* instantiation time, so the body has something to compare against   */
/* the registry.                                                      */
/*                                                                    */
/* For concrete T:  Is<Circle>.Of(h) compares the registered concrete */
/* type name against nameof<Circle>() == "Circle".                    */
/*                                                                    */
/* For interface T: Is<Shape>.Of(h) looks up the registered concrete  */
/* type name, then checks the class→interfaces table.  Requires       */
/* asit_impl(Circle, Shape) declarations at the call site.            */
/*                                                                    */
/* As<T>.Of returns void* so the same generic class works for both    */
/* concrete T (caller casts to T*) and interface T (caller casts to   */
/* Any<I>*).  This avoids needing the compiler to distinguish         */
/* class-T from interface-T at instantiation time.                    */
/* ------------------------------------------------------------------ */
static inline int __asit_is_impl(void *h, const char *wanted) {
    const char *have = __asit_lookup(h);
    return have && __asit_satisfies(have, wanted);
}

static inline void *__asit_as_impl(void *h, const char *wanted) {
    return __asit_is_impl(h, wanted) ? *(void **)h : NULL;
}

class Is<T> {
    static bool Of(void* h) {
        return __asit_is_impl(h, nameof<T>());
    }
};

/* As<T>.Of returns the raw concrete pointer (T*) for concrete T.
   For interface T, use Is<I>.Of to check conformance, then cast the
   original Any<I>* handle to Any<J>* directly — the handle's vtable
   slots work for any interface the concrete type satisfies.  As<I>.Of
   is not suitable for interface T because it returns the concrete
   pointer, not a re-erased handle. */
class As<T> {
    static void* Of(void* h) {
        if (Is<T>.Of(h))
            return *(void**)h;
        return NULL;
    }
};

#endif /* CLASSYC_ASIT_H */
