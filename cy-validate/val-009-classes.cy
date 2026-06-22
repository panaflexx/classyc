/* val-009-classes.cy — validates classes: constructors, destructors, new/delete,
 * fluent method chaining, and defer-driven destructor ordering (LIFO).
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-009-classes.cy -eg
 */
#include <stdio.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* destructor-order log: each Wizard records its sigil as it is banished */
char banish_log[64];
int  banish_len = 0;
void banish(char sigil) { banish_log[banish_len++] = sigil; banish_log[banish_len] = '\0'; }

int summoned = 0;   /* ctor count */
int banished = 0;   /* dtor count */

class Wizard {
    int  mana;
    int  level;
    char sigil;

    Wizard(int mana, int level) { this.mana = mana; this.level = level; this.sigil = '*'; summoned++; }
    Wizard(int mana, int level, char sigil) { this.mana = mana; this.level = level; this.sigil = sigil; summoned++; }
    ~Wizard() { banished++; banish(this.sigil); }

    Wizard* study(int gained)  { this.level += gained; return this; }   /* fluent */
    Wizard* meditate(int gain) { this.mana  += gain;  return this; }    /* fluent */
    int power() { return this.mana + this.level * 10; }
};

void convene_and_dismiss() {
    /* three heap wizards, each dismissed by defer in REVERSE (LIFO) order */
    Wizard* a = new Wizard(10, 1, 'A');
    defer delete a;
    Wizard* b = new Wizard(20, 2, 'B');
    defer delete b;
    Wizard* c = new Wizard(30, 3, 'C');
    defer delete c;
    /* defers run C, B, A on scope exit */
}

int main() {
    printf("=== val-009 classes ===\n\n");

    /* overloaded ctor + field init */
    summoned = 0; banished = 0;
    Wizard* gandalf = new Wizard(50, 5);
    check(gandalf->mana == 50 && gandalf->level == 5, "ctor sets fields");
    check(summoned == 1,        "ctor ran once");

    /* fluent chaining returns this */
    gandalf->study(3)->meditate(20);
    check(gandalf->level == 8 && gandalf->mana == 70, "method chaining mutates same object");
    check(gandalf->power() == 70 + 80, "computed method after chain");

    delete gandalf;
    check(banished == 1,        "delete invokes destructor");

    /* method call chained directly on a new-expression */
    int pw = new Wizard(5, 1, 'X').study(1)->power();
    check(pw == 5 + 20, "chain a method on a new-expression");

    /* named constructor arguments, in and out of declared order */
    Wizard* n1 = new Wizard(mana = 5, level = 2);
    defer delete n1;
    Wizard* n2 = new Wizard(level = 2, mana = 5);
    defer delete n2;
    check(n1->power() == n2->power() && n1->mana == 5 && n2->level == 2,
          "named args work in any order");

    /* defer destructor ordering = LIFO */
    banish_len = 0;
    convene_and_dismiss();
    check(strcmp(banish_log, "CBA") == 0, "defer delete runs destructors in LIFO order");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
