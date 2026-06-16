#include <stdio.h>
#include <stdlib.h>

class Calculator {
    int value;

    Calculator(int v) { this.value = v; }

    int divide(int divisor) {
        if (divisor == 0) {
            fprintf(stderr, "Division by zero!\n");
            abort();
        }
        return this.value / divisor;
    }
};

int compute(Calculator *c, int d) {
    return c->divide(d);
}

int main(int argc, char **argv) {
    Calculator *c = new Calculator(100);
    printf("100 / 5 = %d\n", compute(c, 5));
    printf("100 / 0 = ...\n");
    int result = compute(c, 0);  /* triggers abort */
    printf("result = %d\n", result);
    return 0;
}
