/* Test 019: Class with destructor */
#include <stdio.h>

class Counter {
    int value;
    
    Counter(int value) {
        this.value = value;
    }
    
    ~Counter() {
        printf("Counter(%d) destroyed\n", this.value);
    }
    
    int get() {
        return this.value;
    }
};

int main() {
    Counter c(42);
    printf("counter: %d\n", c.get());
    return 0;
}