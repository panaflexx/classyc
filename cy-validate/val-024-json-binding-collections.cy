/* val-024-json-binding-collections.cy — typed JSON binding Phase 2.

   Extends (T) d / (T)? d to collection-valued fields (List<T>*):
     - List<int>*        from a JSON array of numbers
     - List<String>*     from a JSON array of strings (private copies, owned)
     - List<Point>*      from a JSON array of nested objects (recurses)
     - lenient (T)? d    leaves a missing array field at NULL
     - strict (T) d      throws KeyException on a missing array field

   The bound object owns the heap collection; its destructor must `delete` it
   (as List<T>::~List does).  The source dict can be deleted right after the
   bind — the collection's elements are independent copies.

   Run:  ./bin/classyc -g -I include cy-validate/val-024-json-binding-collections.cy -eg
*/
#include <stdio.h>
#include <string.h>
#include <list.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class Scores {
    String name;
    List<int>* scores;
    Scores() { this.scores = new List<int>(); }
    ~Scores() { delete this.scores; }
};

class Tag {
    String name;
    List<String>* aliases;
    Tag() { this.aliases = new List<String>(); }
    ~Tag() { delete this.aliases; }
};

class Point { int x; int y; };
class Polyline {
    String label;
    List<Point>* pts;
    Polyline() { this.pts = new List<Point>(); }
    ~Polyline() { delete this.pts; }
};

int main() {
    printf("=== val-024 JSON binding Phase 2 (collections) ===\n\n");

    /* --- List<int>* from a JSON number array --- */
    dict d = json("{\"name\":\"ada\",\"scores\":[10,20,30]}");
    defer delete d;
    Scores s = (Scores) d;
    check(strcmp((char*)s.name, "ada") == 0, "List<int>*: String sibling field bound");
    check(s.scores->count() == 3,          "List<int>*: array length bound");
    check(s.scores->Get(0) == 10
          && s.scores->Get(1) == 20
          && s.scores->Get(2) == 30,       "List<int>*: element values bound");

    /* --- List<String>* from a JSON string array (owned private copies) --- */
    /* Use a nested scope so we can delete the source dict mid-function and
       prove the bound strings outlive it, without colliding with a defer. */
    Tag t;
    {
        dict dt = json("{\"name\":\"red\",\"aliases\":[\"crimson\",\"scarlet\",\"ruby\"]}");
        t = (Tag) dt;
        check(strcmp((char*)t.name, "red") == 0,           "List<String>*: String sibling bound");
        check(t.aliases->count() == 3,                      "List<String>*: array length bound");
        check(strcmp((char*)t.aliases->Get(0), "crimson") == 0
              && strcmp((char*)t.aliases->Get(2), "ruby") == 0,
              "List<String>*: string element values bound");
        delete dt;  /* free the source dict arena */
        check(strcmp((char*)t.aliases->Get(1), "scarlet") == 0,
              "List<String>*: elements survive source-dict deletion (owned copies)");
    }
    delete t.aliases;  /* t was built on the stack; free the owned collection manually */

    /* --- List<Point>* from a JSON array of nested objects --- */
    dict dp = json("{\"label\":\"tri\",\"pts\":[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}]}");
    defer delete dp;
    Polyline p = (Polyline) dp;
    check(strcmp((char*)p.label, "tri") == 0,  "List<Point>*: String sibling bound");
    check(p.pts->count() == 2,                  "List<Point>*: array length bound");
    check(p.pts->Get(0).x == 1 && p.pts->Get(0).y == 2
          && p.pts->Get(1).x == 3 && p.pts->Get(1).y == 4,
          "List<Point>*: nested object element fields bound");

    /* --- lenient (T)? d: missing array field -> NULL --- */
    dict dm = json("{\"name\":\"blue\"}");
    defer delete dm;
    Scores sm = (Scores)? dm;
    check(strcmp((char*)sm.name, "blue") == 0,  "lenient: present field bound");
    check(sm.scores == NULL,                     "lenient: missing array field -> NULL");

    /* --- strict (T) d: missing array field throws KeyException --- */
    dict dmiss = json("{\"name\":\"green\"}");
    defer delete dmiss;
    int caught = 0;
    try {
        Scores s2 = (Scores) dmiss;
        (void) s2;
    } catch (KeyException e) {
        caught = 1;
        (void) e;
    }
    check(caught == 1, "strict: missing array field throws KeyException");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
