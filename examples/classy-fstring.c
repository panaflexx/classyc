/* classy-fstring.c — interpolated f-string support.
 *
 *   String name = "bob";
 *   String s = f"hello {name}";      //  ->  "hello bob"
 *
 * An `f`-prefixed narrow string literal is an f-string.  Each `{ expr }` is
 * replaced by the textual value of the embedded C expression; `{{` and `}}`
 * produce literal `{` / `}`.  f-strings lower to the existing String `+`
 * concatenation (with basic-type auto-cast), so any expression whose value can
 * be concatenated may be interpolated: String/char* values, int, unsigned,
 * bool, char, double, function results, member / subscript accesses, etc.
 *
 * Notes / limitations:
 *   - Format specifiers (e.g. Python's {x:.2f}) are not supported.
 *   - Macros are not expanded inside { ... }.
 *   - Like any String concatenation, an f-string with interpolation is a
 *     runtime value and cannot initialise a file-scope (global) variable.
 */

#include <stdio.h>
#include <string.h>

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) {
        printf("  PASS  %s\n", label);
        passed = passed + 1;
    } else {
        printf("  FAIL  %s\n", label);
        failed = failed + 1;
    }
}

struct Point { int x; int y; };

/* f-string built and returned from a helper */
String greet(String who, int age) {
    return f"{who} is {age}";
}

int main() {
    printf("=== f-string test suite ===\n\n");
    passed = 0;
    failed = 0;

    /* ---- the headline example ---- */
    String name = "bob";
    String s = f"hello {name}";
    printf("    %s\n", s);
    check(strcmp(s, "hello bob") == 0, "1  hello {name}");

    /* ---- multiple interpolations + surrounding text ---- */
    int age = 30;
    String s2 = f"hello {name}, age {age}";
    check(strcmp(s2, "hello bob, age 30") == 0, "2  multiple {} with text");

    /* ---- arithmetic expressions inside braces ---- */
    String s3 = f"sum={age + 12} prod={age * 2}";
    check(strcmp(s3, "sum=42 prod=60") == 0, "3  arithmetic in {}");

    /* ---- basic-type auto-cast ---- */
    bool active = 1;
    char grade = 'A';
    unsigned u = 100;
    int neg = -7;
    String s4 = f"active={active} grade={grade} u={u} neg={neg}";
    check(strcmp(s4, "active=true grade=A u=100 neg=-7") == 0, "4  bool/char/unsigned/neg cast");

    /* ---- double auto-cast ---- */
    double pi = 3.5;
    String s5 = f"pi={pi}";
    check(strcmp(s5, "pi=3.5") == 0, "5  double cast");

    /* ---- no interpolation: plain text ---- */
    String s6 = f"just text";
    check(strcmp(s6, "just text") == 0, "6  no interpolation");

    /* ---- starts / ends with an interpolation ---- */
    String s7 = f"{name}!";
    check(strcmp(s7, "bob!") == 0, "7  leading interpolation");

    /* ---- escaped braces ---- */
    String s8 = f"set = {{ {age} }}";
    check(strcmp(s8, "set = { 30 }") == 0, "8  escaped {{ }}");

    /* ---- member and subscript access ---- */
    struct Point p = { 7, 9 };
    String s9 = f"point({p.x}, {p.y})";
    check(strcmp(s9, "point(7, 9)") == 0, "9  member access in {}");

    int arr[3] = { 10, 20, 30 };
    String s10 = f"arr[1]={arr[1]}";
    check(strcmp(s10, "arr[1]=20") == 0, "10 subscript in {}");

    /* ---- function call returning String, interpolated ---- */
    String s11 = f"[{greet(\"Alice\", 42)}]";
    check(strcmp(s11, "[Alice is 42]") == 0, "11 function-call interpolation");

    /* ---- nested: an f-string interpolating another String ---- */
    String s12 = f"<{s}>";
    check(strcmp(s12, "<hello bob>") == 0, "12 interpolate a String var");

    /* ---- used directly as a printf argument ---- */
    printf("    direct: %s\n", f"x={age} y={neg}");
    check(1, "13 f-string as printf arg (visual)");

    /* ---- escape sequences alongside interpolation ---- */
    String s14 = f"a\tb={age}";
    check(strcmp(s14, "a\tb=30") == 0, "14 escape sequence preserved");

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
