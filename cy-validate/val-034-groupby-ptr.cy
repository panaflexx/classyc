/* val-034-groupby-ptr.cy — free-fn inference preserves pointer type args.
 *
 * Regression: GroupBy/ListGroupBy on List<T*> used to monomorphize T as the
 * bare class (Pilot) by stripping the mangled trailing 'P' from
 * __generic_List_PilotP.  The specialized keySelector then expected Pilot by
 * value while the list held Pilot*, and the JIT SIGSEGV'd on first call.
 *
 * Fix: free-fn List_* inference rebuilds type args with N_POINTER depth, so
 *   list->GroupBy(fn) / GroupBy(list, fn)
 * specialize as GroupBy<Pilot*, G>, matching int(*)(Pilot*).
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-034-groupby-ptr.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "map.h"
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class Item {
    int id;
    String tag;
    Item(int id, String tag) { this->id = id; this->tag = tag; }
    ~Item() {}
    int Id() { return this->id; }
};

int item_parity(Item* it) { return it->id % 2; }
int item_tag_bucket(Item* it) {
    const char *s = (const char *)it->tag;
    return (s && s[0]) ? (int)s[0] : 0;
}
int int_parity(int x) { return x % 2; }

int main() {
    printf("=== val-034 GroupBy with pointer element types ===\n\n");

    List<Item*>* xs = new List<Item*>().owns();
    defer delete xs;
    xs->Add(new Item(1, "alpha"));
    xs->Add(new Item(2, "beta"));
    xs->Add(new Item(3, "alpha"));
    xs->Add(new Item(4, "gamma"));
    xs->Add(new Item(5, "beta"));

    /* ── 1. UFCS list->GroupBy on List<Item*> ───────────────────────────── */
    printf("-- 1. UFCS List<Item*>->GroupBy --\n");
    auto by_parity = xs->GroupBy(item_parity);
    check(by_parity.Count() == 2, "1a  two parity buckets");
    auto odd = by_parity.Get(1);
    auto even = by_parity.Get(0);
    check(by_parity.Contains(0) && by_parity.Contains(1), "1b  both buckets present");
    check(odd.Count() == 3, "1c  odd ids (1,3,5)");
    check(even.Count() == 2, "1d  even ids (2,4)");
    check(odd.Get(0)->Id() == 1 && odd.Get(2)->Id() == 5, "1e  odd order + field access");
    check(even.Get(0)->Id() == 2 && even.Get(1)->Id() == 4, "1f  even order");

    /* ── 2. Free GroupBy(list, fn) same specialization ──────────────────── */
    printf("\n-- 2. free GroupBy --\n");
    auto by_tag = GroupBy(xs, item_tag_bucket);
    check(by_tag.Count() == 3, "2a  three tag buckets (a/b/g)");
    check(by_tag.Get((int)'a').Count() == 2, "2b  alpha x2");
    check(by_tag.Get((int)'b').Count() == 2, "2c  beta x2");
    check(by_tag.Get((int)'g').Count() == 1, "2d  gamma x1");

    /* ── 3. ListGroupBy alias still works for pointers ──────────────────── */
    printf("\n-- 3. ListGroupBy alias --\n");
    auto alias = ListGroupBy(xs, item_parity);
    check(alias.Count() == 2 && alias.Get(1).Count() == 3, "3a  ListGroupBy parity");

    /* ── 4. Scalar List<int> regression (still good) ────────────────────── */
    printf("\n-- 4. List<int> GroupBy still works --\n");
    List<int>* nums = new List<int>{10, 11, 12, 13};
    defer delete nums;
    auto ni = nums->GroupBy(int_parity);
    check(ni.Count() == 2 && ni.Get(0).Count() == 2 && ni.Get(1).Count() == 2,
          "4a  int UFCS GroupBy");

    /* ── 5. No call-arg mismatch warnings path: key selector receives Item* ── */
    printf("\n-- 5. nested Select after GroupBy --\n");
    int sum_odd = 0;
    for (auto it in odd) sum_odd += it->Id();
    check(sum_odd == 1 + 3 + 5, "5a  for-in over pointer bucket");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
