#include <stdio.h>
#include <string.h>

class Fruit {
    /* UNIFIED declarative definition: one dict, one source of truth.
       Keys are the variant names.
       Each value is a sub-dict containing the int "value" (for switch/case)
       and any other metadata you want (desc, color, season, ...).

       Compiler sugar:
         - Fruit.Apple   in integer/constant contexts resolves to .value (0)
         - Fruit itself (or .variants) is the dict for runtime:
             for-in, "name" in Fruit, subscript, .json, mutation, etc.
         - Exhaustiveness checking possible on switch using the keys.
    */
    dict variants = {
        "Apple": {
            "value": 0,
            "desc": "A crisp, classic apple."
        },
        "Banana": {
            "value": 10,
            "desc": "Curved yellow banana."
        },
        "Kiwi": {
            "value": 1,
            "desc": "Furry brown kiwi with bright green inside."
        },
        "Mango": {
            "value": 2,
            "desc": "Sweet tropical mango."
        }
    };
};

/* All operations go through the single variants dict. */
void describe(int value) {
    for (auto name in Fruit.variants) {
        auto d = Fruit.variants[name];
        if (d.value == value) {
            printf("%s\n", d.desc);
            return;
        }
    }
    printf("(unknown fruit value %d)\n", value);
}

/* The unified structure shines here: switch on the resolved compile-time
   integer value coming from the same declarative dict that holds the desc. */
void handle_fruit(int f) {
    switch (f) {
        case Fruit.Apple:   /* resolves to the .value from the dict at compile time */
            printf("[switch] Handling %s (action: eat fresh)\n", Fruit.variants.Apple.desc);
            break;
        case Fruit.Banana:
            printf("[switch] Handling %s (action: peel first)\n", Fruit.variants.Banana.desc);
            break;
        case Fruit.Kiwi:
            printf("[switch] Handling %s (action: scoop with spoon)\n", Fruit.variants.Kiwi.desc);
            break;
        case Fruit.Mango:
            printf("[switch] Handling %s (action: slice around pit)\n", Fruit.variants.Mango.desc);
            break;
        /* Compiler can warn: "non-exhaustive switch over Fruit variants"
           if you leave one out and omit default. */
    }
}

int main() {
    int snack = Fruit.Kiwi;   /* compiler extracts the .value (1) */

    printf("Today's snack: Kiwi (value=%d)\n", snack);

    describe(snack);
    describe(Fruit.Mango);
    describe(99);

    printf("\n--- unified switch/case demo ---\n");
    handle_fruit(snack);
    handle_fruit(Fruit.Apple);
    handle_fruit(Fruit.Banana);

    printf("\nAll variants (single for-in over the unified dict):\n");
    for (auto name, data in Fruit.variants) {
        printf("  %s : value=%d  desc=\"%s\"\n",
               name, (int)data.value, data.desc);
    }

    printf("\nMembership: %s\n", ("Kiwi" in Fruit.variants) ? "yes" : "no");

    printf("\nFull declarative structure as JSON:\n%s\n", Fruit.variants.json);

    /* Still fully dynamic because it's just a dict */
    dict live = json(Fruit.variants.json);
    live.Peach = { "value": 99, "desc": "Furry peach." };
    printf("\nAfter runtime addition of Peach:\n%s\n", live.json);

    return 0;
}
