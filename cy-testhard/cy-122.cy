/* Test 122: Complex template/generic specialization */
#include <stdio.h>

// Base template
class Stack<T> {
    List<T>* items;
    Stack() { this.items = new List<T>(); }
    void push(T item) { this.items->Add(item); }
    T pop() {
        int n = this.items->Count();
        T item = this.items->Get(n - 1);
        this.items->RemoveAt(n - 1);
        return item;
    }
    int count() { return this.items->Count(); }
};

// Specialization for String (if supported) - test generic behavior
class StringStack {
    List<String>* items;
    StringStack() { this.items = new List<String>(); }
    void push(String item) { this.items->Add(item); }
    String pop() {
        int n = this.items->Count();
        String item = this.items->Get(n - 1);
        this.items->RemoveAt(n - 1);
        return item;
    }
    int count() { return this.items->Count(); }
    String join(String sep) { return this.items->join(sep); }
};

int main() {
    // Generic Stack<int>
    Stack<int>* intStack = new Stack<int>();
    intStack->push(1);
    intStack->push(2);
    intStack->push(3);
    printf("int stack: %d, %d, %d\n", intStack->pop(), intStack->pop(), intStack->pop());

    // StringStack (manual specialization)
    StringStack* strStack = new StringStack();
    strStack->push("a");
    strStack->push("b");
    strStack->push("c");
    printf("string stack: %s, %s, %s\n", strStack->pop(), strStack->pop(), strStack->pop());
    printf("joined: %s\n", strStack->join(","));

    // Stack of pointers
    Stack<int*>* ptrStack = new Stack<int*>();
    int a = 10, b = 20, c = 30;
    ptrStack->push(&a);
    ptrStack->push(&b);
    ptrStack->push(&c);
    printf("ptr stack: %d, %d, %d\n", *ptrStack->pop(), *ptrStack->pop(), *ptrStack->pop());

    return 0;
}
