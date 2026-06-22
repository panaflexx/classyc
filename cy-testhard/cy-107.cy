/* Test 107: Complex class with operator-like methods and chaining */
#include <stdio.h>

class Builder {
    String result;

    Builder() { this.result = ""; }

    Builder* add(String s) { this.result = this.result + s; return this; }
    Builder* addInt(int n) { this.result = this.result + f"{n}"; return this; }
    Builder* addFloat(float f) { this.result = this.result + f"{f}"; return this; }
    Builder* space() { this.result = this.result + " "; return this; }
    Builder* newline() { this.result = this.result + "\n"; return this; }
    String build() { return this.result; }
};

int main() {
    String s = new Builder()
        ->add("Hello")
        ->space()
        ->add("World")
        ->space()
        ->addInt(42)
        ->space()
        ->addFloat(3.14)
        ->newline()
        ->add("Done")
        ->build();

    printf("built:\n%s", s);
    return 0;
}
