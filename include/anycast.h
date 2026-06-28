/* anycast.h — safe down-casting and type-testing for Any<I> handles
 *
 *   bool         ok = is<Circle>(any_shape);          // concrete type test
 *   Circle*      c  = as<Circle>(any_shape);          // returns raw pointer or NULL
 *
 *   bool         ok2 = is<Drawable>(any_shape);       // interface conformance test
 *   Any<Drawable>* d = as<Drawable>(any_shape);       // returns a new handle or NULL
 *
 * The operations are lowered by the compiler (soft-keyword parsing of
 * `is<` / `as<`) exactly like `any<Interface>(expr)`.  They are completely
 * compatible with the existing Any<I> implementation.
 */
#ifndef CLASSYC_ANYCAST_H
#define CLASSYC_ANYCAST_H

/* ──────────────────────────────────────────────────────────────────────────
   Soft-keyword intrinsics
   ──────────────────────────────────────────────────────────────────────────
   `is` and `as` are soft keywords: they only have special meaning when they
   appear at the start of a primary expression followed by `< Type > ( expr )`.
   Elsewhere they remain ordinary identifiers so existing code is unaffected.
*/

/* Parse-time hook – implemented in the compiler.  The header only provides
   documentation and a hook for IDEs / documentation generators. */
#define is  is
#define as  as

/* Convenience aliases that read more naturally in some code bases. */
#define is_a   is
#define cast_as as

#endif /* CLASSYC_ANYCAST_H */
