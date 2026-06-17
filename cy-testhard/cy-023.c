/* Test 023: defer statement */
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
    Counter c(100);
    defer delete &c;
    printf("defer: %d\n", c.get());
    return 0;
}