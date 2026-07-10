/* val-029 — [[builtin_method]] header-extensible String methods.
 *
 * Proves the attribute path can add a new method name that lowers to an
 * existing runtime helper without a new SM_* case in classyc.c.
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-029-builtin-method-attr.cy -eg
 */
[[builtin_method("String", "upcase", "c2m_str_upper", 0, "String")]]
static int __bm_upcase;

[[builtin_method("String", "downcase", "c2m_str_lower", 0, "String")]]
static int __bm_downcase;

#include <stdio.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int main() {
    printf("=== val-029 builtin_method attribute ===\n\n");

    String s = "MiXeD";
    check(s.upcase().equals("MIXED"),   "attr method upcase -> c2m_str_upper");
    check(s.downcase().equals("mixed"), "attr method downcase -> c2m_str_lower");
    /* Stock methods still work (seeded + redeclared in string_builtins.h). */
    check("  x  ".trim().equals("x"),   "stock trim still works");
    check("abc".length() == 3,          "stock length still works");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
