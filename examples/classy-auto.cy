/* classy-auto.c — C23 `auto` type deduction combined with dict support.
 *
 * Design / disambiguation rule (unambiguous, syntactic):
 *
 *   auto x = <expr>;            -> deduced as the (decayed) type of <expr>
 *   auto x = { v0, v1, ... };   -> KEYLESS brace list  -> array  (e.g. int[N])
 *   auto x = { "k": v, ... };   -> KEYED   brace list  -> dict
 *
 * Keyless braces always mean "array"; a brace list with "key": value entries
 * always means "dict".  Explicit `dict d = { ... }` continues to work exactly
 * as before.  (The keyed/keyless split also leaves room for future generic
 * container forms such as  List<int> nums = {1, 2, 3};)
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

int main() {
    printf("=== auto deduction test suite ===\n\n");
    passed = 0;
    failed = 0;

    /* ---- scalar deduction (C11-style) ---- */
    printf("-- scalar deduction --\n");

    auto i = 42;               /* int    */
    auto d = 3.14;             /* double */
    auto s = "Hello, World!";  /* const char* */

    check(i == 42,                       "1a  auto int value");
    check(d > 3.13 && d < 3.15,          "1b  auto double value");
    check(strcmp(s, "Hello, World!") == 0,"1c  auto string value");
    check(sizeof(i) == sizeof(int),      "1d  auto int has int size");
    check(sizeof(d) == sizeof(double),   "1e  auto double has double size");

    /* ---- keyless brace list -> array (C23) ---- */
    printf("\n-- keyless brace list -> array --\n");

    auto arr = {1, 2, 3};      /* int[3] */
    auto one = {4};            /* int[1] */

    check(arr[0] == 1,                       "2a  arr[0]");
    check(arr[1] == 2,                       "2b  arr[1]");
    check(arr[2] == 3,                       "2c  arr[2]");
    check(sizeof(arr) == 3 * sizeof(int),    "2d  arr is int[3]");
    check(sizeof(arr) / sizeof(arr[0]) == 3, "2e  arr element count == 3");
    check(one[0] == 4,                       "2f  one[0]");
    check(sizeof(one) == sizeof(int),        "2g  one is int[1]");

    /* arrays are mutable and iterable */
    arr[1] = 20;
    check(arr[1] == 20, "2h  arr element is assignable");

    {
        int sum = 0;
        for (auto x in arr)
            sum = sum + x;
        check(sum == 1 + 20 + 3, "2i  for-in over auto array");
    }

    /* deduction follows the element type */
    auto reals = {1.5, 2.5, 3.5};   /* double[3] */
    check(reals[2] > 3.49 && reals[2] < 3.51, "2j  auto double array value");
    check(sizeof(reals) == 3 * sizeof(double), "2k  reals is double[3]");

    /* ---- keyed brace list -> dict ---- */
    printf("\n-- keyed brace list -> dict --\n");

    auto cfg = {
        "host": "localhost",
        "port": 8080
    };
    check(cfg != 0,         "3a  auto dict is non-null");
    check(cfg.host != 0,    "3b  cfg.host present");
    check(cfg.port != 0,    "3c  cfg.port present");

    /* auto dicts behave like explicit dicts: dot read/write + new keys */
    cfg.port = 9090;
    check(cfg.port != 0,    "3d  cfg.port reassigned");
    cfg.scheme = "https";
    check(cfg.scheme != 0,  "3e  cfg.scheme new key");

    /* nested keyed list -> nested dict */
    auto deep = {
        "outer": {
            "inner": 42
        }
    };
    check(deep.outer != 0,        "3f  nested auto dict outer");
    check(deep.outer.inner != 0,  "3g  nested auto dict inner");

    printf("    cfg.json()  = %s\n", cfg.json());
    printf("    deep.json() = %s\n", deep.json());

    /* ---- explicit dict still works alongside auto ---- */
    printf("\n-- explicit dict coexistence --\n");

    dict explicit = { "a": 1, "b": 2 };
    check(explicit.a != 0, "4a  explicit dict.a");
    check(explicit.b != 0, "4b  explicit dict.b");

    /* mix: auto array of ints and a dict in the same scope */
    auto ids = {10, 20, 30};
    dict reg = { "count": 3 };
    check(ids[2] == 30,    "4c  auto array next to dict");
    check(reg.count != 0,  "4d  dict next to auto array");

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
