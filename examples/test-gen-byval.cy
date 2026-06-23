/* test-gen-byval.cy — minimal generic holding a by-value class */
#include <stdio.h>
#include <stdlib.h>

class Track { int id; };

class Vec<T> {
    T* data;
    int len;
    Vec() { this->len = 0; this->data = (T*) malloc(sizeof(T) * 4); }
    ~Vec() { if (this->data) free((void*) this->data); }
    int Count() { return this->len; }
    T Get(int i) { return this->data[i]; }
    void Add(T item) { this->data[this->len] = item; this->len = this->len + 1; }
    Vec<T>* Concat(Vec<T>* other) {
        for (auto item in other) this->Add(item);
        return this;
    }
    void Sort(int(*cmp)(T, T)) {
        int gap = this->len / 2;
        while (gap > 0) {
            for (int i = gap; i < this->len; i++) {
                T tmp = this->data[i];
                int j = i;
                while (j >= gap && cmp(this->data[j - gap], tmp) > 0) {
                    this->data[j] = this->data[j - gap];
                    j = j - gap;
                }
                this->data[j] = tmp;
            }
            gap = gap / 2;
        }
    }
    Vec<T>* Map(T(*fn)(T)) {
        Vec<T>* result = new Vec<T>();
        for (int i = 0; i < this->len; i++)
            result->Add(fn(this->data[i]));
        return result;
    }
};

int main() {
    Vec<Track>* v = new Vec<Track>();
    delete v;
    printf("ok\n");
    return 0;
}
