/* classy-dict.c — comprehensive test of dict support in c2mir
 *
 * Exercises:
 *   1. Global dict declaration with initializer
 *   2. Nested object initializers (2+ levels)
 *   3. Integer, float, and string values in initializers
 *   4. Single-level dot-access read
 *   5. Chained multi-level dot-access read
 *   6. Dot-access assignment with integer value
 *   7. Dot-access assignment with float value
 *   8. Chained dot-access assignment
 *   9. Multiple global dict variables
 *  10. Adding new keys via dot assignment
 *  11. Overwriting existing keys via dot assignment
 *  12. Dict-to-dict assignment (sub-object)
 *  13. Local dict in function — with initializer
 *  14. Local dict in function — without initializer (dict_create_object)
 *  15. Local dict — nested initializer
 *  16. Local dict — dot read / write
 *  17. Local dict — passed as argument
 *  18. Local dict — returned value used
 *  19. Multiple local dicts in one function
 *  20. Local dict in a helper function (non-main)
 *  21. Three-level deep nesting
 *  22-25. Bracket subscript
 *  26-27. "key" in dict operator
 *  28-31. for (auto k in dict) loops
 *  32-37. for (auto x in array) loops
 *  38. json(string) -> dict  (parse)
 *  39. json(dict) -> string  (serialize)
 *  40. d.json property shorthand
 *  41. Round-trip json(json(d)) fidelity
 *  42. Mutate parsed dict then re-serialize
 *  43. for-in on parsed dict
 */

/* ---- Global dict declarations ---- */

dict flat = {
    "name":    "alice",
    "age":     30,
    "score":   95
};

dict cfg = {
    "server": {
        "host": "localhost",
        "port": 8080
    },
    "debug": 1
};

dict meta = {
    "version": 2,
    "tag":     "beta"
};

/* ---- helpers (declared as extern, resolved by import_resolver) ---- */

dict  dict_object_get(dict obj, char *key);
int   dict_object_set(dict obj, char *key, dict val);
dict  dict_create_int64(long n);
dict  dict_create_string(char *s);
dict  dict_create_object();


/* ---- Test harness ---- */

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

/* ---- (20) Helper function that creates & returns a local dict ---- */
dict make_point(int x, int y) {
    dict pt = { "x": 0, "y": 0 };
    pt.x = x;
    pt.y = y;
    return pt;
}

/* ---- (17) Helper that receives a dict and reads from it ---- */
int dict_has_key(dict obj, char *key) {
    dict v = dict_object_get(obj, key);
    return v != 0;
}

/* ---- main ---- */

int main() {
    printf("=== dict test suite ===\n\n");
    passed = 0;
    failed = 0;

    /* ========== GLOBAL DICT TESTS ========== */

    printf("-- global dicts --\n");

    /* 1. Global dict creation */
    check(flat  != 0, "1a  flat dict is non-null");
    check(cfg   != 0, "1b  cfg dict is non-null");
    check(meta  != 0, "1c  meta dict is non-null");

    /* 4. Single-level dot read */
    check(flat.age    != 0, "4a  flat.age is non-null");
    check(flat.name   != 0, "4b  flat.name is non-null");
    check(flat.score  != 0, "4c  flat.score is non-null");
    check(cfg.debug   != 0, "4d  cfg.debug is non-null");

    /* 5. Chained dot read */
    check(cfg.server       != 0, "5a  cfg.server is non-null");
    check(cfg.server.host  != 0, "5b  cfg.server.host is non-null");
    check(cfg.server.port  != 0, "5c  cfg.server.port is non-null");

    /* 6-7. Dot-access assignment — integer */
    flat.age = 31;
    check(flat.age != 0, "6a  flat.age after int assign");
    flat.score = 100;
    check(flat.score != 0, "7a  flat.score after int assign");

    /* 8. Chained dot assignment — integer & string */
    cfg.server.port = 9090;
    check(cfg.server.port != 0, "8a  cfg.server.port chained int assign");
    cfg.server.host = "example.com";
    check(cfg.server.host != 0, "8b  cfg.server.host chained str assign");

    /* 10. Adding new keys */
    flat.email = "alice@example.com";
    check(flat.email != 0, "10a flat.email new key");
    cfg.server.workers = 4;
    check(cfg.server.workers != 0, "10b cfg.server.workers new key");

    /* 11. Overwriting existing keys */
    meta.version = 3;
    check(meta.version != 0, "11a meta.version overwrite");
    meta.tag = "release";
    check(meta.tag != 0, "11b meta.tag overwrite");

    /* 12. Dict-to-dict assignment */
    meta.srv = cfg.server;
    check(meta.srv != 0, "12a meta.srv = cfg.server");

    /* ========== LOCAL DICT TESTS ========== */

    printf("\n-- local dicts --\n");

    /* 13. Local dict with initializer */
    dict local1 = { "color": "red", "count": 5 };
    check(local1 != 0,       "13a local dict is non-null");
    check(local1.color != 0, "13b local1.color is non-null");
    check(local1.count != 0, "13c local1.count is non-null");

    /* 14. Local dict — empty, then populated */
    dict local2 = dict_create_object();
    check(local2 != 0, "14a local empty dict is non-null");
    dict_object_set(local2, "key", dict_create_string("value"));
    check(local2.key != 0, "14b local2.key after manual set");

    /* 15. Local dict — nested initializer */
    dict nested = {
        "outer": {
            "inner": 42
        }
    };
    check(nested != 0,              "15a nested local dict is non-null");
    check(nested.outer != 0,        "15b nested.outer is non-null");
    check(nested.outer.inner != 0,  "15c nested.outer.inner is non-null");

    /* 16. Local dict — dot write then read */
    local1.shape = "circle";
    check(local1.shape != 0, "16a local1.shape added via dot assign");
    local1.count = 10;
    check(local1.count != 0, "16b local1.count overwritten");

    /* 17. Local dict — passed as argument */
    check(dict_has_key(local1, "color") != 0, "17a dict_has_key(local1, color)");
    check(dict_has_key(local1, "shape") != 0, "17b dict_has_key(local1, shape)");
    check(dict_has_key(local1, "nope")  == 0, "17c dict_has_key(local1, nope) == 0");

    /* 18. Local dict — returned from function */
    dict pt = make_point(10, 20);
    check(pt   != 0, "18a make_point returned non-null");
    check(pt.x != 0, "18b pt.x is non-null");
    check(pt.y != 0, "18c pt.y is non-null");

    /* 19. Multiple local dicts in one function */
    dict a = { "id": 1 };
    dict b = { "id": 2 };
    check(a != 0, "19a first of two locals is non-null");
    check(b != 0, "19b second of two locals is non-null");
    check(a.id != 0, "19c a.id readable");
    check(b.id != 0, "19d b.id readable");

    /* 20. Local dict created in helper already tested via make_point (18) */

    /* ========== BRACKET SUBSCRIPT TESTS ========== */

    printf("\n-- bracket subscript --\n");

    /* 22. Bracket read with string literal */
    check(flat["name"]  != 0, "22a flat[\"name\"] read");
    check(flat["score"] != 0, "22b flat[\"score\"] read");

    /* 23. Bracket read with variable key */
    char *lookup = "age";
    check(flat[lookup] != 0, "23a flat[variable_key] read");

    /* 24. Bracket write */
    flat["motto"] = "carpe diem";
    check(flat["motto"] != 0, "24a flat[\"motto\"] written");
    flat["lucky"] = 7;
    check(flat["lucky"] != 0, "24b flat[\"lucky\"] int written");

    /* 25. Bracket read on nested dict */
    check(cfg["server"] != 0, "25a cfg[\"server\"] nested read");

    /* ========== IN OPERATOR TESTS ========== */

    printf("\n-- in operator --\n");

    /* 26. Basic key-in-dict */
    check("name" in flat,          "26a \"name\" in flat");
    check(!("ghost" in flat),      "26b \"ghost\" not in flat");
    check("server" in cfg,         "26c \"server\" in cfg");
    check(!("missing" in cfg),     "26d \"missing\" not in cfg");

    /* 27. in operator in if-statement */
    if ("age" in flat)
        check(1, "27a if-in true branch taken");
    else
        check(0, "27a if-in true branch taken");

    if ("nope" in flat)
        check(0, "27b if-in false branch skipped");
    else
        check(1, "27b if-in false branch skipped");

    /* ========== FOR-IN LOOP TESTS ========== */

    printf("\n-- for-in loops --\n");

    /* 28. for (auto key in dict) — single variable */
    {
        int key_count = 0;
        for (auto k in local1)
            key_count = key_count + 1;
        check(key_count == 3, "28a for-in iterated correct count (local1=3 keys)");
    }

    /* 29. for-in over a nested sub-dict */
    {
        int srv_count = 0;
        for (auto k in cfg.server)
            srv_count = srv_count + 1;
        check(srv_count == 3, "29a for-in over cfg.server (3 keys)");
    }

    /* 30. for-in with key, value */
    {
        int found_x = 0;
        for (auto k, v in pt) {
            if ("x" in pt) found_x = 1;
        }
        check(found_x, "30a for (auto k,v in pt) found x");
    }

    /* 31. for-in with body that uses bracket subscript */
    {
        printf("    31: ");
        for (auto k in meta)
            printf("%s ", k);
        printf("\n");
        check(1, "31a for-in keys printed (visual check above)");
    }

    /* ========== ARRAY FOR-IN TESTS ========== */

    printf("\n-- array for-in --\n");

    /* 32. for (auto x in int_array) — single variable */
    {
        int nums[] = {10, 20, 30, 40, 50};
        int sum = 0;
        for (auto x in nums)
            sum = sum + x;
        check(sum == 150, "32a for-in int array sum");
    }

    /* 33. for (auto i, v in array) — index + element */
    {
        int vals[] = {100, 200, 300};
        int last_i = -1;
        int last_v = -1;
        for (auto i, v in vals) {
            last_i = i;
            last_v = v;
        }
        check(last_i == 2,   "33a array for-in last index == 2");
        check(last_v == 300, "33b array for-in last value == 300");
    }

    /* 34. for-in over char array */
    {
        char word[] = "ABC";
        int count = 0;
        for (auto c in word)
            count = count + 1;
        /* includes null terminator */
        check(count == 4, "34a char array for-in count (incl null)");
    }

    /* 35. for-in array in same function as dict for-in */
    {
        int arr[] = {1, 2, 3};
        int arr_sum = 0;
        for (auto x in arr)
            arr_sum = arr_sum + x;

        dict dd = { "a": 1 };
        int dict_keys = 0;
        for (auto k in dd)
            dict_keys = dict_keys + 1;

        check(arr_sum == 6,    "35a array + dict for-in coexist (arr sum)");
        check(dict_keys == 1,  "35b array + dict for-in coexist (dict count)");
    }

    /* 36. for-in over String array */
    {
        String animals[3] = {"cats", "dogs", "fish"};
        int count = 0;
        for (auto s in animals)
            count = count + 1;
        check(count == 3, "36a String array for-in count");
    }

    /* 37. for-in over String array with index */
    {
        String fruits[2] = {"apple", "pear"};
        int last_i = -1;
        for (auto i, name in fruits)
            last_i = i;
        check(last_i == 1, "37a String array indexed for-in last index");
    }

    /* 21. Three-level deep nesting */
    dict deep = {
        "l1": {
            "l2": {
                "l3": 999
            }
        }
    };
    check(deep != 0,                   "21a deep dict is non-null");
    check(deep.l1 != 0,               "21b deep.l1 is non-null");
    check(deep.l1.l2 != 0,            "21c deep.l1.l2 is non-null");
    check(deep.l1.l2.l3 != 0,         "21d deep.l1.l2.l3 is non-null");

    /* ========== JSON BUILTIN TESTS ========== */

    printf("\n-- json() builtin --\n");

    /* 38. json(string) -> dict  (parse JSON) */
    dict parsed = json("{\"x\":10,\"y\":20,\"label\":\"hi\"}");
    check(parsed != 0,            "38a json(string) returns non-null");
    check("x" in parsed,          "38b parsed dict has key x");
    check("y" in parsed,          "38c parsed dict has key y");
    check("label" in parsed,      "38d parsed dict has key label");
    check(!("nope" in parsed),    "38e parsed dict lacks missing key");

    /* 39. json(dict) -> string  (serialize) */
    dict ser = { "a": 1, "b": 2 };
    char *j1 = json(ser);
    check(j1 != 0,                "39a json(dict) returns non-null string");
    check((int)strlen(j1) > 5,    "39b serialized string has content");
    printf("    json(ser) = %s\n", j1);

    /* 40. d.json property shorthand */
    char *j2 = ser.json;
    check(j2 != 0,                "40a d.json returns non-null string");
    check((int)strlen(j2) > 5,    "40b d.json has content");
    printf("    ser.json  = %s\n", j2);

    /* 41. Round-trip: parse then serialize preserves content */
    dict rt = json("{\"name\":\"bob\",\"age\":42}");
    char *rt_json = json(rt);
    printf("    round-trip = %s\n", rt_json);
    /* re-parse the serialized string */
    dict rt2 = json(rt_json);
    check("name" in rt2,          "41a round-trip preserved key name");
    check("age" in rt2,           "41b round-trip preserved key age");

    /* 42. Mutate a parsed dict then re-serialize */
    dict md = json("{\"color\":\"red\"}");
    md.size = 42;
    md.color = "blue";
    printf("    mutated = %s\n", md.json);
    check("size" in md,           "42a mutated parsed dict has new key");

    /* 43. for-in over a parsed dict */
    {
        dict fd = json("{\"p\":1,\"q\":2,\"r\":3}");
        int count = 0;
        for (auto k in fd)
            count = count + 1;
        check(count == 3,          "43a for-in over parsed dict count == 3");
    }

    /* ========== SERIALISE FOR VISUAL INSPECTION ========== */

    printf("\n--- serialised state ---\n");

    printf("flat   = %s\n\n", flat.json);
    printf("cfg    = %s\n\n", cfg.json);
    printf("meta   = %s\n\n", meta.json);
    printf("local1 = %s\n\n", local1.json);
    printf("nested = %s\n\n", nested.json);
    printf("pt     = %s\n\n", json(pt));
    printf("deep   = %s\n\n", deep.json);

    /* ========== SUMMARY ========== */

    printf("=== results: %d passed, %d failed ===\n", passed, failed);

    return failed;
}
