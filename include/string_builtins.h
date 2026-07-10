/* string_builtins.h — header-extensible String builtin method table
 *
 * ClassyC recognizes `s.method(...)` on the built-in `String` type by looking
 * up a data-driven registry.  The compiler seeds the stock methods; this header
 * re-declares them with C23 attributes so new methods can be added here (plus a
 * matching c2m_str_* runtime function) without editing classyc.c.
 *
 * Attribute form:
 *   [[builtin_method(type, method, runtime_symbol, nargs, retkind [, "static"])]]
 *
 * type:
 *   "String"       — instance method on a String value / string literal
 *   "StringStatic" — type method: String.copy(...)  (or pass "static" as 6th arg)
 *   "ListString"   — method on List<String> (join)
 *
 * retkind: "String" | "size_t" | "int" | "char*" | "ListString"
 *
 * The dummy `static int` declarator only exists so attributes have a place to
 * hang; it is never referenced at runtime.
 */
#ifndef CLASSYC_STRING_BUILTINS_H
#define CLASSYC_STRING_BUILTINS_H

/* ── Instance methods ─────────────────────────────────────────────────── */

[[builtin_method("String", "length", "c2m_str_length", 0, "size_t")]]
static int __bm_str_length;

[[builtin_method("String", "empty", "c2m_str_empty", 0, "int")]]
static int __bm_str_empty;

[[builtin_method("String", "substr", "c2m_str_substr", 2, "String")]]
static int __bm_str_substr;

[[builtin_method("String", "find", "c2m_str_find", 1, "size_t")]]
static int __bm_str_find;

/* replace is overloaded by arity (3 = positional, 2 = search-and-replace) */
[[builtin_method("String", "replace", "c2m_str_replace", 3, "String")]]
static int __bm_str_replace;

[[builtin_method("String", "replace", "c2m_str_replace_all", 2, "String")]]
static int __bm_str_replace_all;

[[builtin_method("String", "upper", "c2m_str_upper", 0, "String")]]
static int __bm_str_upper;

[[builtin_method("String", "lower", "c2m_str_lower", 0, "String")]]
static int __bm_str_lower;

[[builtin_method("String", "detach", "c2m_str_detach", 0, "char*")]]
static int __bm_str_detach;

[[builtin_method("String", "starts_with", "c2m_str_starts_with", 1, "int")]]
static int __bm_str_starts_with;

[[builtin_method("String", "ends_with", "c2m_str_ends_with", 1, "int")]]
static int __bm_str_ends_with;

[[builtin_method("String", "contains", "c2m_str_contains", 1, "int")]]
static int __bm_str_contains;

[[builtin_method("String", "trim", "c2m_str_trim", 0, "String")]]
static int __bm_str_trim;

[[builtin_method("String", "split", "c2m_str_split", 1, "ListString")]]
static int __bm_str_split;

[[builtin_method("String", "equals", "c2m_str_equals", 1, "int")]]
static int __bm_str_equals;

/* ── Static type methods ──────────────────────────────────────────────── */

[[builtin_method("String", "copy", "c2m_str_copy", 2, "String", "static")]]
static int __bm_str_copy;

[[builtin_method("String", "attach", "c2m_str_attach", 1, "String", "static")]]
static int __bm_str_attach;

/* ── List<String> methods that lower to the string runtime ────────────── */

[[builtin_method("ListString", "join", "c2m_str_join", 1, "String")]]
static int __bm_str_join;

#endif /* CLASSYC_STRING_BUILTINS_H */
