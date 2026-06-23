/* val-020-json-binding.cy — typed JSON-to-class binding (Phase 1).
 *
 * Covers the JSONBINDING.md Phase 1 spec:
 *   - `(T)  d` strict   : missing field throws KeyException("missing field 'F' in T")
 *   - `(T)? d` lenient  : missing/skipped fields default to 0 / NULL
 *   - Both forms recurse into nested by-value class members.
 *   - Scalars and `String` leaves go through the existing dict-union unwrap path.
 *
 * Spelling note: the `?` marker sits *outside* the closing paren — `(T)? d`
 * rather than `(T?) d`.  Inside-paren `?` collides with the lambda /
 * param-type-list lookahead chain in primary_expr and corrupts subsequent
 * declarations.  Same semantics either way.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-020-json-binding.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class Address { String city; int zip; };
class User    { String name; int age; Address addr; };

/* Plain C structs participate in the bind too — same per-member walk, no
   methods / ctors needed.  Cross-kind nesting (class with struct field,
   struct with class field) also works since both use the same N_MEMBER list. */
struct Point { int x; int y; };
struct Pixel { struct Point pos; int rgba; };
class  Sprite { String name; struct Pixel pixel; };

int main() {
    printf("=== val-020 typed JSON binding ===\n\n");

    /* --- Strict cast, all fields present --- */
    dict d_full = json(
        "{\"name\":\"Ada\",\"age\":36,"
        " \"addr\":{\"city\":\"London\",\"zip\":12345}}");

    User u = (User) d_full;
    check(strcmp((char*)u.name, "Ada") == 0,            "strict: String field bound");
    check(u.age == 36,                                  "strict: int field bound");
    check(strcmp((char*)u.addr.city, "London") == 0,    "strict: nested String field bound");
    check(u.addr.zip == 12345,                          "strict: nested int field bound");

    /* The source dict is still walkable after binding (no consume). */
    check(strcmp((char*)json(d_full.addr),
                 "{\"city\":\"London\",\"zip\":12345}") == 0,
          "strict: source dict still serializable after bind");

    /* --- Strict cast, missing field --- */
    dict d_missing = json("{\"name\":\"Bob\"}");
    int caught_key = 0;
    String caught_msg = "";
    try {
        User u2 = (User) d_missing;
        (void) u2;
    } catch (KeyException e) {
        caught_key = 1;
        caught_msg = e.msg;
    }
    check(caught_key == 1,                              "strict: missing field throws KeyException");
    check(strstr((char*)caught_msg, "missing field") != NULL,
          "strict: KeyException msg starts with 'missing field'");
    check(strstr((char*)caught_msg, "User") != NULL,
          "strict: KeyException msg names the target class");

    /* --- Lenient cast, missing field --- */
    User u3 = (User)? d_missing;
    check(strcmp((char*)u3.name, "Bob") == 0,           "lenient: present field bound");
    check(u3.age == 0,                                  "lenient: missing scalar -> 0");
    check(u3.addr.city == NULL,                         "lenient: missing nested String -> NULL");
    check(u3.addr.zip == 0,                             "lenient: missing nested int -> 0");

    /* --- Lenient propagates: present nested object but missing inner field --- */
    dict d_partial_nested = json(
        "{\"name\":\"Cy\",\"age\":1,\"addr\":{\"city\":\"NYC\"}}");
    User u4 = (User)? d_partial_nested;
    check(strcmp((char*)u4.name, "Cy") == 0,            "lenient: top-level fields populated");
    check(u4.age == 1,                                  "lenient: top-level int populated");
    check(strcmp((char*)u4.addr.city, "NYC") == 0,      "lenient: nested present field populated");
    check(u4.addr.zip == 0,                             "lenient: nested missing field -> 0");

    /* --- Plain C struct cast --- */
    dict d_pt = json("{\"x\":3,\"y\":4}");
    struct Point p = (struct Point) d_pt;
    check(p.x == 3,                                     "struct: scalar x bound");
    check(p.y == 4,                                     "struct: scalar y bound");

    /* --- struct with nested struct field --- */
    dict d_pix = json("{\"pos\":{\"x\":10,\"y\":20},\"rgba\":255}");
    struct Pixel px = (struct Pixel) d_pix;
    check(px.pos.x == 10,                               "struct: nested struct.x bound");
    check(px.pos.y == 20,                               "struct: nested struct.y bound");
    check(px.rgba == 255,                               "struct: sibling scalar bound");

    /* --- class with struct field (cross-kind nesting) --- */
    dict d_spr = json(
        "{\"name\":\"ship\","
        " \"pixel\":{\"pos\":{\"x\":1,\"y\":2},\"rgba\":42}}");
    Sprite s = (Sprite) d_spr;
    check(strcmp((char*)s.name, "ship") == 0,           "class+struct: String at top level");
    check(s.pixel.pos.x == 1 && s.pixel.pos.y == 2,     "class+struct: doubly-nested struct fields");
    check(s.pixel.rgba == 42,                           "class+struct: nested-struct sibling scalar");

    /* --- Lenient struct cast: same defaults as for classes --- */
    dict d_partial_pt = json("{\"x\":7}");
    struct Point p2 = (struct Point)? d_partial_pt;
    check(p2.x == 7,                                    "lenient struct: present scalar bound");
    check(p2.y == 0,                                    "lenient struct: missing scalar -> 0");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
