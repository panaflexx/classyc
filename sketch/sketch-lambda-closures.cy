/* sketch-lambda-closures.c — proposed lambda closure examples for ClassyC
 *
 * These demonstrate what working closures could look like, integrated with
 * existing language features (String, dict, List<T>, defer, for-in, f-strings).
 *
 * Proposed syntax (fits current style):
 *   [captured] (params) => expr
 *   [captured] (params) => { stmts; return ...; }
 *
 * Captures are by reference (mutable) unless specified otherwise.
 */

#include <stdio.h>
#include <string.h>

/* ────────────────────────────────────────────────────────────────────────── */

int passed = 0;
int failed = 0;

void check(int cond, char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* ══════════════════════════════════════════════════════════════════════════
   Example 1: Stateful counter (generator / iterator factory)
   ═════════════════════════════════════════════════════════════════════════ */

auto make_counter(int start) {
    int i = start;
    return [i]() => { i = i + 1; return i - 1; };
}

void example_counter() {
    printf("-- Example 1: Stateful counter --\n");

    auto next = make_counter(10);
    check(next() == 10, "counter starts at 10");
    check(next() == 11, "second call returns 11");
    check(next() == 12, "third call returns 12");

    auto other = make_counter(100);
    check(other() == 100, "independent counter starts at 100");
    check(next()  == 13,  "first counter continues independently");
}

/* ══════════════════════════════════════════════════════════════════════════
   Example 2: Configurable predicate (closes over String + int threshold)
   ═════════════════════════════════════════════════════════════════════════ */

void example_configurable_filter() {
    printf("\n-- Example 2: Configurable filter --\n");

    String prefix = "error";
    int min_code = 500;

    /* Imagine we have a list of log entries as dicts */
    dict logs[4] = {
        {"level": "info",  "code": 200, "msg": "ok"},
        {"level": "error", "code": 404, "msg": "not found"},
        {"level": "error", "code": 500, "msg": "boom"},
        {"level": "error", "code": 503, "msg": "unavailable"}
    };

    /* Closure captures prefix and min_code by reference */
    auto is_severe_error = [prefix, min_code](dict entry) =>
        strcmp(entry.level, prefix) == 0 && (int)entry.code >= min_code;

    int severe_count = 0;
    for (auto e in logs) {
        if (is_severe_error(e)) severe_count++;
    }
    check(severe_count == 2, "found 2 severe errors (500+)");

    /* We can also use it with a hypothetical .filter() if List supported it */
    printf("    (would work with List<dict>.filter(is_severe_error) too)\n");
}

/* ══════════════════════════════════════════════════════════════════════════
   Example 3: Callback with captured context (logging wrapper)
   ═════════════════════════════════════════════════════════════════════════ */

auto make_logged_action(String name, auto action) {
    return [name, action](int x) => {
        printf("  [LOG] entering %s(%d)\n", name, x);
        int result = action(x);
        printf("  [LOG] leaving %s -> %d\n", name, result);
        return result;
    };
}

void example_callback_context() {
    printf("\n-- Example 3: Callback with captured context --\n");

    auto double_it = (int x) => x * 2;
    auto logged_double = make_logged_action("double", double_it);

    int r = logged_double(21);
    check(r == 42, "logged double produced 42");

    /* Another use: rate limiter style (captures last_call time) */
    long last = 0;
    auto rate_limited = [last](int val) => {
        /* In real code we'd check elapsed time */
        last = last + 1;   /* pretend we advanced time */
        return val + (int)last;
    };

    check(rate_limited(5) == 6, "first rate-limited call");
    check(rate_limited(5) == 7, "second rate-limited call");
}

/* ══════════════════════════════════════════════════════════════════════════
   Example 4: Closures with generics (List<T>.filter / map using captured state)
   ═════════════════════════════════════════════════════════════════════════ */

void example_generics_with_closures() {
    printf("\n-- Example 4: Closures + List<T> --\n");

    List<int>* numbers = new List<int>();
    for (int i = 1; i <= 10; i++) numbers->Add(i);
    defer delete numbers;

    /* Capture a threshold from the outer scope */
    int threshold = 5;
    String label   = "big";

    /* Closure passed to a generic List method */
    auto big_ones = numbers->filter([threshold](int x) => x > threshold);
    check(big_ones->Count() == 5, "filter with captured threshold");

    /* Another closure capturing a different variable + String */
    int multiplier = 10;
    auto scaled = numbers->map([multiplier, label](int x) => {
        printf("    scaling %d for %s\n", x, label);
        return x * multiplier;
    });
    check(scaled->Count() == 10, "map with two captured vars");
    check(scaled->Get(9) == 100, "last mapped value correct");
}

/* ══════════════════════════════════════════════════════════════════════════ */

int main() {
    printf("=== Lambda Closures Sketch ===\n\n");

    example_counter();
    example_configurable_filter();
    example_callback_context();
    example_generics_with_closures();

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
