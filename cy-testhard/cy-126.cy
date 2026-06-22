/* Test 126: Complex preprocessor interactions with ClassyC features */
#include <stdio.h>

#define STRINGIFY(x) #x
#define CONCAT(a, b) a##b
#define MAKE_CLASS(name) class CONCAT(name, Class) { int value; }

MAKE_CLASS(Foo)
MAKE_CLASS(Bar)

#define DECLARE_METHOD(cls, ret, name, ...) \
    ret CONCAT(cls, _##name)(CONCAT(cls, Class)* this, __VA_ARGS__)

DECLARE_METHOD(Foo, int, getValue)
DECLARE_METHOD(Foo, void, setValue, int v)

int Foo_getValue(FooClass* this) { return this->value; }
void Foo_setValue(FooClass* this, int v) { this->value = v; }

#define TEST_MACRO(x) f"macro: {x}"

int main() {
    FooClass* f = new FooClass();
    Foo_setValue(f, 42);
    printf("macro class: %d\n", Foo_getValue(f));

    String s = TEST_MACRO("expanded");
    printf("stringify macro: %s\n", s);

    // Preprocessor with ClassyC types
    #define MAKE_DICT(name, ...) dict name = { __VA_ARGS__ }

    MAKE_DICT(config, "key1", 1, "key2", 2);
    printf("macro dict: %d, %d\n", config.key1, config.key2);

    // Conditional compilation with ClassyC
    #ifdef DEBUG
    String debug = "debug mode";
    #else
    String debug = "release mode";
    #endif
    printf("build: %s\n", debug);

    return 0;
}
