/* Minimal test: List<char*> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class List<T> {
    T*  data;
    int length;
    int capacity;

    List() {
        this.length   = 0;
        this.capacity = 4;
        this.data     = (T*) malloc(sizeof(T) * this.capacity);
    }

    ~List() {
        if (this.data) free((void*) this.data);
    }

    int Count() { return this.length; }
    T Get(int index) { return this.data[index]; }

    void Add(T item) {
        if (this.length >= this.capacity) {
            int newCap = this.capacity * 2;
            T* newData = (T*) malloc(sizeof(T) * newCap);
            for (int i = 0; i < this.length; i++) newData[i] = this.data[i];
            free((void*) this.data);
            this.data     = newData;
            this.capacity = newCap;
        }
        this.data[this.length] = item;
        this.length++;
    }
};

int main() {
    List<char*>* strs = new List<char*>();
    strs->Add("hello");
    strs->Add("world");
    printf("count = %d\n", strs->Count());
    printf("[0] = %s\n", strs->Get(0));
    printf("[1] = %s\n", strs->Get(1));
    delete strs;
    printf("OK\n");
    return 0;
}
