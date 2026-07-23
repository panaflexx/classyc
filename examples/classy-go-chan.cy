/* classy-go-chan.cy — Go-style fibers + channels (-ffibers)
 *
 * The FIBERS.md target program, plus a multi-worker pipeline:
 *
 *   go worker(ch);          spawn a fiber (value-packed args, max 8, GP-class)
 *   await;                  pure cooperative yield (fiber state check)
 *   Chan<T>(cap)            typed channel over cchan; send/recv park explicitly
 *   add_scheduler(n);       explicit runtime: cy_sched_init(n) + cy_sched_run()
 *
 * Rules (v1):
 *   - `go` takes a direct plain-function call (wrap methods in a function).
 *   - `await` is a pure yield; channel parking lives inside Chan<T> ops.
 *   - Chan.send throws RuntimeException on send-after-close (Go panics);
 *     Chan.close throws on double close.
 *   - Share channels by pointer; stop → close → let the scheduler drain.
 *
 * JIT:  ./bin/classyc -I include -ffibers examples/classy-go-chan.cy -eg
 * AOT:  ./classyc-aot.sh -I include -ffibers examples/classy-go-chan.cy -o /tmp/gochan && /tmp/gochan
 */
#include "chan.h"
#include <stdatomic.h>

void worker(Chan<int> *ch) {
    for (int i = 0; i < 100; i++)
        ch->send(i);              /* parks if full — never blocks the worker */
    ch->close();
}

/* ── multi-worker pipeline: produce → square → sum ─────────────────────── */
static Chan<long> *pipe_in;
static Chan<long> *pipe_out;
static atomic_llong pipe_sum;

void produce(long n) {
    for (long i = 1; i <= n; i++) pipe_in->send(i);
    pipe_in->close();
}

void square(void) {
    long v = 0;
    while (pipe_in->recv(&v)) {
        pipe_out->send(v * v);
        await;                    /* pure yield: give the sink a turn */
    }
    pipe_out->close();
}

void sink(void) {
    long v = 0;
    while (pipe_out->recv(&v)) atomic_fetch_add(&pipe_sum, v);
}

int main(void) {
    int fails = 0;

    /* 1. The FIBERS.md program: buffered chan, one fiber, main consumes. */
    {
        auto ch = new Chan<int>(16);
        go worker(ch);
        int sum = 0, v = 0;
        for (;;) {
            if (!ch->recv(&v)) break;   /* false after close+drain */
            sum += v;
        }
        add_scheduler(1);
        if (sum == 4950) printf("PASS fib-program sum=4950\n");
        else { printf("FAIL fib-program sum=%d\n", sum); fails++; }
        delete ch;
        cy_sched_shutdown();
    }

    /* 2. Four-worker pipeline through two channels. */
    pipe_in = new Chan<long>(32);
    pipe_out = new Chan<long>(32);
    atomic_store(&pipe_sum, 0LL);
    go produce(100);
    go square();
    go sink();
    add_scheduler(4);
    {
        long long want = 338350LL;    /* 1^2+…+100^2 */
        long long got = atomic_load(&pipe_sum);
        if (got == want) printf("PASS pipeline sumsq=%lld\n", got);
        else { printf("FAIL pipeline sumsq=%lld want=%lld\n", got, want); fails++; }
    }
    delete pipe_in;
    delete pipe_out;
    cy_sched_shutdown();

    /* 3. Unbuffered rendezvous + send-after-close throw. */
    {
        auto ch = new Chan<int>();    /* capacity 0: rendezvous */
        go worker(ch);
        int n = 0, v = 0;
        while (ch->recv(&v)) n++;
        add_scheduler(1);
        if (n == 100) printf("PASS rendezvous drained 100\n");
        else { printf("FAIL rendezvous n=%d\n", n); fails++; }

        int threw = 0;
        try {
            ch->send(1);              /* closed → RuntimeException */
        } catch (Exception e) {
            threw = 1;
        }
        if (threw) printf("PASS send-on-closed threw\n");
        else { printf("FAIL send-on-closed did not throw\n"); fails++; }
        delete ch;
        cy_sched_shutdown();
    }

    printf(fails == 0 ? "GO/CHAN SMOKE PASSED\n" : "GO/CHAN SMOKE FAILED (%d)\n", fails);
    return fails;
}
