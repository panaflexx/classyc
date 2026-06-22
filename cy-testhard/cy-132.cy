/* Test 132: Complex attribute and alignment scenarios */
#include <stdio.h>

__attribute__((aligned(64))) class CacheLine {
    int data[16];
};

__attribute__((packed)) class Packed {
    char a;
    int b;
    short c;
    char d;
};

class Normal {
    char a;
    int b;
    short c;
    char d;
};

__attribute__((noreturn)) void abort_func() {
    printf("aborting...\n");
    while(1) {}
}

__attribute__((const)) int pure_func(int x) {
    return x * x;
}

__attribute__((malloc)) void* my_alloc(size_t size) {
    return malloc(size);
}

int main() {
    printf("CacheLine size: %zu (aligned 64)\n", sizeof(CacheLine));
    printf("Packed size: %zu\n", sizeof(Packed));
    printf("Normal size: %zu\n", sizeof(Normal));

    CacheLine* cl = new CacheLine();
    printf("CacheLine addr: %p (aligned=%d)\n", cl, ((uintptr_t)cl % 64) == 0);
    delete cl;

    Packed p = { .a = 1, .b = 0x12345678, .c = 0xABCD, .d = 2 };
    printf("packed: a=%d b=0x%x c=0x%x d=%d\n", p.a, p.b, p.c, p.d);

    printf("pure_func(5) = %d\n", pure_func(5));
    printf("pure_func(5) = %d (cached?)\n", pure_func(5));
   spit
    void* ptr = my_alloc(100);
    printf("my_alloc: %p\n", ptr);
    free(ptr);

    return 0;
}
