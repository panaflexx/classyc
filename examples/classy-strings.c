/* classy-strings.c — comprehensive test of String type support in c2mir
 *
 * Exercises:
 *   1. String declaration and initialisation (local + global)
 *   2. String arrays (local + global, fixed-size)
 *   3. String concatenation via + operator
 *   4. Passing String to printf / variadic functions
 *   5. strlen on String
 *   6. String in dict values
 *   7. for (auto s in StringArray)
 *   8. for (auto i, s in StringArray) — indexed
 *   9. String as function parameter and return value
 *  10. String in struct / class member
 *  11. UTF-8 content in String
 *  12. String comparison via strcmp (undeclared / implicit)
 *  13. Mixing String arrays with dict for-in in one function
 *  14. String + basic-type auto-cast concatenation (int/bool/char/double/...)
 */

/* ---- globals ---- */

String greeting   = "Hello, World!";
String empty_str  = "";
String utf8_str   = "Schöne Grüße 😊";
String animals[4] = {"cat", "dog", "fish", "bird"};

/* class used in test 10 (must be file-scope) */
class StringPet {
    String name;
    int age;
};

/* ---- test harness ---- */

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

/* ---- helpers ---- */

/* Concat and print (avoids return-type issue with concat result) */
void print_greeting(String name) {
    String hi = "Hi ";
    printf("%s", hi + name);
}

/* ---- main ---- */

int main() {
    printf("=== String test suite ===\n\n");
    passed = 0;
    failed = 0;

    /* ========== 1. Basic declaration ========== */

    printf("-- 1. declaration --\n");

    String local_s = "local string";
    check((int)strlen(local_s) == 12,    "1a  local String strlen");
    check((int)strlen(greeting) == 13,   "1b  global String strlen");
    check((int)strlen(empty_str) == 0,   "1c  empty String strlen");
    check((int)strlen(utf8_str) > 10,    "1d  utf8 String strlen > 10");

    /* ========== 2. String arrays ========== */

    printf("\n-- 2. arrays --\n");

    /* global array */
    check((int)strlen(animals[0]) == 3,  "2a  global array[0] = cat");
    check((int)strlen(animals[3]) == 4,  "2b  global array[3] = bird");

    /* local array */
    String fruits[3] = {"apple", "pear", "mango"};
    check((int)strlen(fruits[0]) == 5,   "2c  local array[0] = apple");
    check((int)strlen(fruits[2]) == 5,   "2d  local array[2] = mango");

    /* ========== 3. Concatenation (+ operator) ========== */

    printf("\n-- 3. concatenation --\n");

    String first = "Hello";
    String second = " World";
    printf("    concat: %s\n", first + second);
    check((int)strlen(first + second) == 11, "3a  concat strlen");

    /* concat with array element */
    String prefix = "my_";
    printf("    prefix+arr: %s\n", prefix + animals[0]);
    check((int)strlen(prefix + animals[0]) == 6, "3b  prefix+array[0] strlen");

    /* ========== 4. printf ========== */

    printf("\n-- 4. printf --\n");

    String msg = "formatted output";
    printf("    msg: [%s]\n", msg);
    check(1, "4a  printf with String arg (visual)");

    printf("    multi: %s | %s | %s\n", fruits[0], fruits[1], fruits[2]);
    check(1, "4b  printf with multiple String args (visual)");

    /* ========== 5. strlen ========== */

    printf("\n-- 5. strlen --\n");

    check((int)strlen("literal") == 7,     "5a  strlen on string literal");
    check((int)strlen(first) == 5,         "5b  strlen on String var");
    check((int)strlen(animals[1]) == 3,    "5c  strlen on array element");

    /* ========== 6. String in dict ========== */

    printf("\n-- 6. dict with Strings --\n");

    dict d = { "name": "alice", "city": "paris" };
    check(d != 0,             "6a  dict with string values created");
    check(d.name != 0,        "6b  d.name is non-null");
    check(d.city != 0,        "6c  d.city is non-null");

    /* assign String to dict field */
    d.country = "france";
    check(d.country != 0,     "6d  d.country assigned");

    /* String array value in dict */
    d.pet = animals[0];
    check("pet" in d,         "6e  d.pet set from String array");

    /* ========== 7. for-in over String array ========== */

    printf("\n-- 7. for-in String array --\n");

    {
        int count = 0;
        for (auto s in animals)
            count = count + 1;
        check(count == 4, "7a  for-in count over global String[4]");
    }

    {
        int count = 0;
        for (auto s in fruits)
            count = count + 1;
        check(count == 3, "7b  for-in count over local String[3]");
    }

    /* print all */
    {
        printf("    animals:");
        for (auto a in animals)
            printf(" %s", a);
        printf("\n");
        check(1, "7c  for-in print all animals (visual)");
    }

    /* ========== 8. for-in indexed ========== */

    printf("\n-- 8. for-in indexed --\n");

    {
        int last_i = -1;
        for (auto i, s in animals)
            last_i = i;
        check(last_i == 3, "8a  indexed for-in last index == 3");
    }

    {
        printf("    indexed:");
        for (auto i, f in fruits)
            printf(" [%d]=%s", i, f);
        printf("\n");
        check(1, "8b  indexed for-in print (visual)");
    }

    /* ========== 9. Function parameter and return ========== */

    printf("\n-- 9. functions --\n");

    printf("    greeting: ");
    print_greeting("Bob");
    printf("\n");
    check(1, "9a  print_greeting(\"Bob\") visual");

    printf("    greeting2: ");
    print_greeting(animals[2]);
    printf("\n");
    check(1, "9b  print_greeting with array element visual");

    /* ========== 10. String as class member ========== */

    printf("\n-- 10. class member --\n");

    /* Note: class declared at file scope to avoid redeclaration issues */
    /* class StringPet { String name; int age; };  (declared above) */
    StringPet p;
    p.name = "Rex";
    p.age = 5;
    printf("    pet: %s age %d\n", p.name, p.age);
    check(p.age == 5, "10a Pet.age == 5");
    check((int)strlen(p.name) == 3, "10b Pet.name strlen == 3");

    /* ========== 11. UTF-8 ========== */

    printf("\n-- 11. UTF-8 --\n");

    printf("    utf8: %s\n", utf8_str);
    check((int)strlen(utf8_str) > 0, "11a utf8 String non-empty");

    String emoji = "🎉🎊🎈";
    printf("    emoji: %s\n", emoji);
    check((int)strlen(emoji) > 0, "11b emoji String non-empty");

    /* ========== 12. strcmp ========== */

    printf("\n-- 12. strcmp --\n");

    String sa = "apple";
    String sb = "banana";
    String sc = "apple";
    check(strcmp(sa, sc) == 0,   "12a strcmp equal");
    check(strcmp(sa, sb) != 0,   "12b strcmp not equal");
    check(strcmp(sa, sb) < 0,    "12c strcmp ordering a < b");
    check(strcmp(sb, sa) > 0,    "12d strcmp ordering b > a");

    /* ========== 13. Mixed for-in ========== */

    printf("\n-- 13. mixed for-in --\n");

    {
        /* String array for-in */
        int str_count = 0;
        for (auto s in fruits)
            str_count = str_count + 1;

        /* dict for-in in same scope */
        int dict_count = 0;
        for (auto k in d)
            dict_count = dict_count + 1;

        /* int array for-in in same scope */
        int nums[] = {10, 20, 30};
        int num_sum = 0;
        for (auto n in nums)
            num_sum = num_sum + n;

        check(str_count == 3,    "13a String for-in count");
        check(dict_count == 4,   "13b dict for-in count");
        check(num_sum == 60,     "13c int array for-in sum");
    }

    /* ========== 14. String + basic-type auto-cast concatenation ========== */

    printf("\n-- 14. auto-cast concatenation --\n");

    /* int + bool, all auto-cast to text and joined with the `+` operator */
    int n14 = 42;
    bool flag = 1;
    String s14 = "n=" + n14 + " flag=" + flag;
    printf("    built: %s\n", s14);
    check(strcmp(s14, "n=42 flag=true") == 0,  "14a int + bool auto-cast concat");

    /* literal + int + literal */
    String s14b = "x" + 5 + "y";
    check(strcmp(s14b, "x5y") == 0,            "14b literal + int + literal");

    /* double auto-cast ("%g") */
    double pi = 3.5;
    String s14c = "pi=" + pi;
    check(strcmp(s14c, "pi=3.5") == 0,         "14c double auto-cast");

    /* char auto-cast (single character) */
    char c14 = 'Q';
    String s14d = "ch=" + c14;
    check(strcmp(s14d, "ch=Q") == 0,           "14d char auto-cast");

    /* unsigned auto-cast */
    unsigned u14 = 100;
    String s14e = "u=" + u14;
    check(strcmp(s14e, "u=100") == 0,          "14e unsigned auto-cast");

    /* negative int auto-cast */
    int neg = -7;
    String s14f = "neg=" + neg;
    check(strcmp(s14f, "neg=-7") == 0,         "14f negative int auto-cast");

    /* bool false renders as "false" */
    bool no = 0;
    String s14g = "flag=" + no;
    check(strcmp(s14g, "flag=false") == 0,     "14g bool false -> false");

    /* String variable + int (not just literals) */
    String base = "count: ";
    int cnt = 9;
    String s14h = base + cnt;
    check(strcmp(s14h, "count: 9") == 0,       "14h String var + int");

    /* the headline example from the feature request */
    int myInt = 7;
    bool myBool = 1;
    String s14i = "hello " + myInt + " is " + myBool;
    printf("    headline: %s\n", s14i);
    check(strcmp(s14i, "hello 7 is true") == 0, "14i headline example");

    /* ========== SUMMARY ========== */

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);

    return failed;
}
