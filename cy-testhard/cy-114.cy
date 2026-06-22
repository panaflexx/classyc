/* Test 114: Generic constraints and specialization edge cases */
#include <stdio.h>

class Box<T> {
    T value;
    Box(T v) { this.value = v; }
    T get() { return this.value; }
    void set(T v) { this.value = v; }
};

class Processor {
    // Process any box
    static void process(Box<int>* b) { printf("int box: %d\n", b->get()); }
    static void process(Box<String>* b) { printf("string box: %s\n", b->get()); }
    static void process(Box<float>* b) { printf("float box: %f\n", b->get()); }
};

int main() {
    Box<int>* bi = new Box<int>(42);
    Box<String>* bs = new Box<String>("hello");
    Box<float>* bf = new Box<float>(3.14);

    Processor::process(bi);
    Processor::process(bs);
    Processor::process(bf);

    // Nested generics
    Box<Box<int>*>* nested = new Box<Box<int>*>(bi);
    printf("nested: %d\n", nested->get()->get());

    // Array of generic boxes
    Box<int>* boxes[] = { new Box<int>(1), new Box<int>(2), new Box<int>(3) };
    for (auto b in boxes) printf("box: %d\n", b->get());

    return 0;
}
