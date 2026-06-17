/* Test 032: Class with static members */
#include <stdio.h>

class Counter {
    static int count;
    int value;
    
    Counter(int value) {
        this.value = value;
        Counter.count++;
    }
    
    static int getCount() {
        return Counter.count;
    }
};

int Counter::count = 0;

int main() {
    Counter c1(10);
    Counter c2(20);
    printf("static count: %d\n", Counter.getCount());
    return 0;
}