/* multi-worker go + await: producer/consumer pairs across 4 workers */
#include "chan.h"
#include <stdatomic.h>

static Chan<int> *q1;
static Chan<int> *q2;
static atomic_llong g_sum;

void producer(long n) {
    for (long i = 1; i <= n; i++) q1->send((int) i);
    q1->close();
}

void doubler(void) {
    int v = 0;
    while (q1->recv(&v)) {
        q2->send(v * 2);
        await;                       /* pure yield: let the sink run */
    }
    q2->close();
}

void sink(void) {
    int v = 0;
    for (;;) {
        int rc = q2->try_recv(&v);
        if (rc < 0) break;
        if (rc == 0) { await; continue; }   /* state check + yield */
        atomic_fetch_add(&g_sum, v);
    }
}

int main(void) {
    q1 = new Chan<int>(8);
    q2 = new Chan<int>(8);
    atomic_store(&g_sum, 0LL);
    go producer(1000);
    go doubler();
    go sink();
    add_scheduler(4);
    long long got = atomic_load(&g_sum);
    long long want = 2LL * 1000 * 1001 / 2;
    printf("sum=%lld want=%lld\n", got, want);
    int ok = got == want;
    delete q1; delete q2;
    cy_sched_shutdown();
    return ok ? 0 : 1;
}
