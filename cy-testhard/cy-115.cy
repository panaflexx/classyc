/* Test 115: Complex struct/class with bitfields and alignment */
#include <stdio.h>

class BitPacked {
    unsigned int a : 1;
    unsigned int b : 3;
    unsigned int c : 4;
    unsigned int d : 8;
    unsigned int e : 16;
};

class Aligned {
    alignas(16) int x;
    alignas(32) double y;
    char padding[10];
};

int main() {
    BitPacked bp = { .a = 1, .b = 7, .c = 15, .d = 255, .e = 65535 };
    printf("bitpacked: a=%u b=%u c=%u d=%u e=%u\n", bp.a, bp.b, bp.c, bp.d, bp.e);
    printf("sizeof BitPacked: %zu\n", sizeof(BitPacked));

    Aligned a = { .x = 42, .y = 3.14 };
    printf("aligned: x=%d y=%f sizeof=%zu\n", a.x, a.y, sizeof(Aligned));

    // Test with new/delete
    BitPacked* bp2 = new BitPacked();
    bp2->a = 0; bp2->b = 3; bp2->c = 8; bp2->d = 128; bp2->e = 32768;
    printf("heap bitpacked: a=%u b=%u c=%u d=%u e=%u\n", bp2->a, bp2->b, bp2->c, bp2->d, bp2->e);
    delete bp2;

    return 0;
}
