/* fib-smoke1: single-thread scheduler, main consumes while producer fiber runs. */
#include "chan.h"
#include <stdatomic.h>

void producer(void *arg) {
    Chan<int> *ch = (Chan<int>*) arg;
    for (int i = 1; i <= 100; i++) ch->send(i);
    ch->close();
}

int main(void) {
    auto ch = new Chan<int>(8);
    defer delete ch;

    cy_spawn(producer, ch);

    int sum = 0, v = 0;
    while (ch->recv(&v)) sum += v;   /* main parks → scheduler steps producer */

    add_scheduler(1);                /* drains anything left; returns at once */
    printf("sum=%d (want 5050)\n", sum);
    return sum == 5050 ? 0 : 1;
}
