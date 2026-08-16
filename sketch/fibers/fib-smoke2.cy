/* fib-smoke2: 4-worker pool; producer → doubler → sink fiber pipeline. */
#include "chan.h"
#include <stdatomic.h>

static Chan<int> *q1;
static Chan<int> *q2;
static atomic_llong g_sum;

void producer(void *arg) {
    (void) arg;
    for (int i = 1; i <= 1000; i++) q1->send(i);
    q1->close();
}

void doubler(void *arg) {
    int v = 0;
    (void) arg;
    while (q1->recv(&v)) q2->send(v * 2);
    q2->close();
}

void sink(void *arg) {
    int v = 0;
    (void) arg;
    while (q2->recv(&v)) atomic_fetch_add(&g_sum, v);
}

int main(void) {
    q1 = new Chan<int>(16);
    q2 = new Chan<int>(16);
    atomic_store(&g_sum, 0LL);

    cy_spawn(producer, 0);
    cy_spawn(doubler, 0);
    cy_spawn(sink, 0);

    add_scheduler(4);                /* init 4 workers, block until drained */

    long long got = atomic_load(&g_sum);
    long long want = 2LL * 1000 * 1001 / 2;
    printf("sum=%lld want=%lld\n", got, want);
    int ok = got == want;
    delete q1;
    delete q2;
    cy_sched_shutdown();
    return ok ? 0 : 1;
}
