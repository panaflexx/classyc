/* Test 125: Async/await-like patterns with lambdas and callbacks */
#include <stdio.h>

typedef void (*Callback)(int result);

void asyncAdd(int a, int b, Callback cb) {
    // Simulate async
    int result = a + b;
    cb(result);
}

void asyncMultiply(int a, int b, Callback cb) {
    int result = a * b;
    cb(result);
}

// Continuation-passing style
void compute(int x, int y, Callback final) {
    asyncAdd(x, y, (int sum) => {
        asyncMultiply(sum, 2, (int prod) => {
            asyncAdd(prod, 10, (int final_result) => {
                final(final_result);
            });
        });
    });
}

int main() {
    int final_result = 0;
    int done = 0;

    compute(5, 3, (int result) => {
        final_result = result;
        done = 1;
        printf("async result: %d\n", result);
    });

    // In real async, we'd wait. Here it's synchronous.
    printf("final: %d (done=%d)\n", final_result, done);

    // Promise-like chaining with lambdas
    auto then = (int value, auto next) => next(value);

    int result = then(10, (int x) => then(x + 5, (int y) => then(y * 2, (int z) => z)));
    printf("chained: %d\n", result);

    return 0;
}
