/* Test 134: Complex string manipulation and encoding edge cases */
#include <stdio.h>

int main() {
    // Various Unicode scenarios
    String ascii = "hello";
    String latin1 = "café";
    String cyrillic = "привет";
    String cjk = "你好世界";
    String emoji = "🎉🎊🎈";
    String mixed = "Hello 世界 😊 café";

    printf("ascii: len=%zu '%s'\n", ascii.length(), ascii);
    printf("latin1: len=%zu '%s'\n", latin1.length(), latin1);
    printf("cyrillic: len=%zu '%s'\n", cyrillic.length(), cyrillic);
    printf("cjk: len=%zu '%s'\n", cjk.length(), cjk);
    printf("emoji: len=%zu '%s'\n", emoji.length(), emoji);
    printf("mixed: len=%zu '%s'\n", mixed.length(), mixed);

    // Find with Unicode
    printf("find 世: %zu\n", mixed.find("世"));
    printf("find 😊: %zu\n", mixed.find("😊"));
    printf("find xyz: %zu\n", mixed.find("xyz"));

    // Replace with Unicode
    String replaced = mixed.replace("世界", "World").replace("😊", ":)");
    printf("replaced: '%s'\n", replaced);

    // Substr with Unicode (byte-based)
    String sub = mixed.substr(0, 10);
    printf("substr(0,10): '%s'\n", sub);

    // Split/Join with Unicode
    String csv = "a,b,c,世界,😊";
    auto parts = csv.split(",");
    printf("split parts: %d\n", parts->Count());
    for (auto p in parts) printf("  '%s'\n", p);
    String joined = parts->join("|");
    printf("joined: '%s'\n", joined);

    // Upper/Lower with Unicode
    String upper = mixed.upper();
    String lower = mixed.lower();
    printf("upper: '%s'\n", upper);
    printf("lower: '%s'\n", lower);

    // Trim with Unicode whitespace
    String with_spaces = "  \t\n  hello  \n\t  ";
    String trimmed = with_spaces.trim();
    printf("trimmed: '%s' (len=%zu)\n", trimmed, trimmed.length());

    return 0;
}
