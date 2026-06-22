/* val-011-fstring-auto.cy — validates f-strings and `auto` disambiguation.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-011-fstring-auto.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("=== val-011 f-strings + auto ===\n\n");

    /* f-string interpolation of variables */
    String user = "bob";
    int score = 42;
    String msg = f"Hello {user}, your score is {score}";
    check(strcmp(msg, "Hello bob, your score is 42") == 0, "f-string interpolates name + int");

    /* f-string with an expression */
    int a = 3, b = 4;
    String sum = f"{a}+{b}={a + b}";
    check(strcmp(sum, "3+4=7") == 0, "f-string interpolates an expression");

    /* f-string used directly as a printf format */
    printf(f"  (visual) score={score}\n");
    check(1, "f-string as printf argument (visual)");

    /* auto disambiguation: scalar */
    auto x = 42;
    check(x == 42, "auto x = 42 -> int");

    /* auto disambiguation: array literal {..} -> array */
    auto arr = {1, 2, 3};
    int s = 0;
    for (auto v in arr) s += v;
    check(s == 6, "auto arr = {1,2,3} -> int[3] (for-in sums to 6)");

    /* auto disambiguation: key:value literal -> dict */
    auto d = {"name": "ada", "age": 36};
    check(d != 0 && "name" in d && (int)d.age == 36, "auto d = {k:v} -> dict");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
