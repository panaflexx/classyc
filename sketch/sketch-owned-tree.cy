/* sketch-owned-tree.cy — exploring the managed-ownership layer in anger.
 *
 *   Part A: a deep object tree (Company > Department > Team > Employee) whose
 *           ROOT is an `owned` local.  Internal ownership lives in the nodes'
 *           destructors (each node deletes its children); `readonly` gives safe
 *           non-owning views into the tree.  Dropping the owned root cascades
 *           the whole tree's cleanup with no `defer delete` anywhere.
 *
 *   Part B: how List<T> and Map<K,V> interact with owned / move:
 *           - an `owned` owning-collection local that auto-frees itself AND its
 *             elements at scope exit (no defer),
 *           - `move`-ing an owned object INTO an owning collection (ownership
 *             hand-off; exactly one free),
 *           - `readonly` views of collection elements.
 *
 * Run:  ./bin/classyc -g -I include sketch/sketch-owned-tree.cy -eg
 *       ./bin/classyc -g -I include -fownership-report sketch/sketch-owned-tree.cy -eg
 */
#include <stdio.h>
#include "list.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* ─────────────────────────── Part A: the tree ─────────────────────────── */

int emp_dtors = 0, team_dtors = 0, dept_dtors = 0, corp_dtors = 0;

class Employee {
    String name;
    Employee(String n) { this.name = n; }
    ~Employee() { emp_dtors++; }
    String who() { return this.name; }
};

class Team {
    String name;
    List<Employee*>* members;     // the Team OWNS its employees
    Team(String n) {
        this.name = n;
        this.members = new List<Employee*>().owns();
    }
    ~Team() { delete this.members; team_dtors++; }   // cascade: frees employees
    Team* hire(Employee* e) { this.members->Add(e); return this; }
    int size() { return this.members->Count(); }
};

class Department {
    String name;
    List<Team*>* teams;           // the Department OWNS its teams
    Department(String n) {
        this.name = n;
        this.teams = new List<Team*>().owns();
    }
    ~Department() { delete this.teams; dept_dtors++; }
    Department* addTeam(Team* t) { this.teams->Add(t); return this; }
    Team* team(int i) { return this.teams->Get(i); }
};

class Company {
    String name;
    List<Department*>* depts;     // the Company OWNS its departments
    Company(String n) {
        this.name = n;
        this.depts = new List<Department*>().owns();
    }
    ~Company() { delete this.depts; corp_dtors++; }
    Company* addDept(Department* d) { this.depts->Add(d); return this; }
    Department* dept(int i) { return this.depts->Get(i); }
};

void part_a() {
    printf("Part A: deep owned tree\n");

    /* The whole tree hangs off a single `owned` root local.  Nothing below is
     * `defer delete`d — ownership lives in the nodes (destructor cascade). */
    owned auto corp = new Company("Acme");

    Department* eng = new Department("Engineering");
    Team* backend = new Team("Backend");
    backend->hire(new Employee("Ada"))
           ->hire(new Employee("Linus"));
    Team* frontend = new Team("Frontend");
    frontend->hire(new Employee("Grace"));
    eng->addTeam(backend)->addTeam(frontend);
    corp->addDept(eng);

    Department* sales = new Department("Sales");
    Team* field = new Team("Field");
    field->hire(new Employee("Don"))->hire(new Employee("Peggy"));
    sales->addTeam(field);
    corp->addDept(sales);

    /* `readonly` borrows: navigate the tree without taking ownership. */
    auto firstDept   = readonly corp->dept(0);
    auto firstTeam   = readonly firstDept->team(0);
    check(firstTeam->size() == 2, "(A) backend team has 2 members via readonly views");
    printf("    %s / %s / %d members (via readonly views)\n",
           firstDept->name, firstTeam->name, firstTeam->size());

    check(emp_dtors == 0 && corp_dtors == 0, "(A) nothing freed while corp is live");
    /* corp goes out of scope here -> ~Company -> ~Department(x2) -> ~Team(x3)
     * -> ~Employee(x5), all automatic. */
}

/* ───────────────────── Part B: List / Map with owned ──────────────────── */

int box_dtors = 0;
class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() { box_dtors++; }
    int get() { return this.v; }
};

void part_b_owning_list() {
    printf("Part B1: owned owning-list local (no defer delete)\n");
    box_dtors = 0;
    {
        /* The list binding is itself `owned`: auto-deleted at scope exit, and
         * because it `.owns()` its elements, deleting it frees them too. */
        owned auto boxes = new List<Box*>().owns();
        boxes->Add(new Box(1));
        boxes->Add(new Box(2));
        boxes->Add(new Box(3));
        check(boxes->Count() == 3, "(B1) list holds 3 boxes");
        check(box_dtors == 0, "(B1) nothing freed yet");
    }
    check(box_dtors == 3, "(B1) owned owning-list freed itself AND 3 elements");
}

void part_b_move_into_collection() {
    printf("Part B2: move an owned object INTO an owning collection\n");
    box_dtors = 0;
    {
        owned auto a = new Box(10);
        owned auto b = new Box(20);
        owned auto bin = new List<Box*>().owns();
        /* Hand ownership of a and b to the list.  `move` CONSUMES a and b —
         * they are dead afterwards (reading them is a compile error); the list
         * is the sole owner. */
        bin->Add(move a);
        bin->Add(move b);
        check(bin->Count() == 2, "(B2) list took both boxes");
        check(bin->Get(0)->get() == 10, "(B2) value readable via the new owner (list)");
        check(box_dtors == 0, "(B2) no premature free");
    }
    check(box_dtors == 2, "(B2) exactly two frees (no double free of moved boxes)");
}

void part_b_map_owns_values() {
    printf("Part B3: owned Map that owns its values\n");
    box_dtors = 0;
    {
        owned auto reg = new Map<String, Box*>().ownsValues();
        reg->Set("one", new Box(100));
        reg->Set("two", move (new Box(200)));   // move of a fresh value: no-op transfer
        check(reg->Count() == 2, "(B3) map holds 2 values");

        auto v = readonly reg->Get("one");      // readonly borrow of a value
        check(v->get() == 100, "(B3) readonly borrow of map value");
        check(box_dtors == 0, "(B3) nothing freed yet");
    }
    check(box_dtors == 2, "(B3) owned value-owning map freed both values");
}

int main() {
    printf("=== sketch: owned / readonly object tree + collections ===\n\n");

    part_a();
    check(emp_dtors == 5,  "(A) all 5 employees freed by cascade");
    check(team_dtors == 3, "(A) all 3 teams freed by cascade");
    check(dept_dtors == 2, "(A) both departments freed by cascade");
    check(corp_dtors == 1, "(A) company freed exactly once (owned root)");
    printf("\n");

    part_b_owning_list();
    printf("\n");
    part_b_move_into_collection();
    printf("\n");
    part_b_map_owns_values();

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
