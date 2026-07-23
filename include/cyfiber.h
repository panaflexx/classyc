/* cyfiber.h — Cooperative fibers + channels runtime for ClassyC
 *
 * Go-style concurrency on top of ext/ccchan (cchan + minicoro):
 *
 *   go worker(ch);            // compiler syntax (-ffibers) → cy_spawn8(...)
 *   await;                    // pure cooperative yield      → cy_yield()
 *   add_scheduler(4);         // explicit runtime: init 4 workers + run
 *
 * Scheduler model (v1):
 *   - Stackful fibers (minicoro), cooperative: a fiber runs until it yields
 *     (await / cy_yield) or parks inside a channel op (cy_chan_*_park).
 *   - cy_sched_init(n<=1): single-OS-thread scheduler pumped by cy_sched_run()
 *     on the calling thread (and by park loops via cy_sched_step).
 *   - cy_sched_init(n>1):  pool of n pthread workers; every fiber is PINNED
 *     to one worker for its whole life (never resumed cross-thread).
 *   - Channel traffic between any pair of fibers/threads is safe: cchan has
 *     its own mutexes.  Fibers never call raw blocking cchan_send/recv —
 *     only try_* + yield (park), so a blocked fiber never stalls its worker.
 *   - Shutdown order per program: producers stop → channels close →
 *     scheduler drains → cy_sched_run returns.
 *
 * Where the implementation lives (declarations below are always visible):
 *   JIT  (-eg): compiled into the `classyc` driver from src/cyfiber.c and
 *               bound by the driver's import_resolver.
 *   AOT:        src/mir-aot-runtime.c under #ifdef CHANFIBERS
 *               (classyc-aot.sh -ffibers compiles it with -DCHANFIBERS).
 *   Host tool:  #define CYFIBER_IMPLEMENTATION in exactly one C TU that
 *               includes this header (needs -I ext/ccchan).
 *
 * Caveats (v1):
 *   - await / channel parking is cooperative: a fiber that blocks in a raw
 *     syscall or spins forever without yielding stalls its whole worker.
 *   - The exception runtime (cyexc) keeps its frame stack per-thread
 *     (_Thread_local), so try/catch works independently on every worker.
 *     A throw still unwinds only the CURRENT thread's stack: always catch
 *     inside the same fiber (or the same worker) that throws.
 *   - The String/object arenas are per-thread, and all fibers on one worker
 *     share that worker's arena.  String values sent through channels are raw
 *     pointers: `detach` (or heap-copy) a String before handing it to another
 *     fiber if the sender may release its arena, and `attach` on the receiver.
 */
#ifndef CLASSYC_CYFIBER_H
#define CLASSYC_CYFIBER_H

/* ── Opaque handles ─────────────────────────────────────────────────────── */
typedef struct cy_fiber cy_fiber;
typedef struct cy_chan_impl *cy_chan;

typedef void (*cy_fiber_fn)(void *arg);

/* ── Scheduler (explicit runtime) ───────────────────────────────────────── */
void      cy_sched_init(int n_workers); /* <=1: caller-thread; >1: worker pool */
void      cy_sched_run(void);           /* block until every fiber has finished */
void      cy_sched_shutdown(void);      /* destroy remaining fibers, reset */
void      add_scheduler(int threads);   /* { cy_sched_init(threads); cy_sched_run(); } */
void      add_schedular(int threads);   /* alias of add_scheduler */

/* ── Fibers ─────────────────────────────────────────────────────────────── */
cy_fiber *cy_spawn(cy_fiber_fn fn, void *arg);
/* Lowering target of `go f(a0,…,aN)` (N ≤ 8, GP-class args only).
   Packs the args on the heap and spawns a trampoline fiber. */
void      cy_spawn8(void *fn, long nargs,
                    long a0, long a1, long a2, long a3,
                    long a4, long a5, long a6, long a7);
void      cy_yield(void);               /* `await` target; safe to call outside fibers */
cy_fiber *cy_self(void);
long      cy_fiber_outstanding(void);   /* number of live fibers */
void      cy_sleep_ms(int ms);          /* fiber-friendly sleep (yields) */

/* ── Channels (thin typed-by-size wrapper over cchan) ─────────────────────
   capacity 0 = unbuffered rendezvous (Go: make(chan T)), >0 = buffered.
   Park ops never block the OS thread: try → yield → retry. */
cy_chan   cy_chan_create(int capacity, int msg_size);
int       cy_chan_send_park(cy_chan c, const void *msg); /* 1 sent, 0 closed */
int       cy_chan_recv_park(cy_chan c, void *msg);       /* 1 got, 0 closed+drained */
int       cy_chan_try_send(cy_chan c, const void *msg);  /* 1 sent, 0 full, -1 closed */
int       cy_chan_try_recv(cy_chan c, void *msg);        /* 1 got, 0 empty, -1 closed+drained */
int       cy_chan_send_timeout(cy_chan c, const void *msg, int ms); /* 1 / 0 timeout / -1 closed */
int       cy_chan_recv_timeout(cy_chan c, void *msg, int ms);
int       cy_chan_close(cy_chan c);                      /* 1 closed now, 0 already closed */
int       cy_chan_is_closed(cy_chan c);
int       cy_chan_size(cy_chan c);                       /* buffered depth */
int       cy_chan_capacity(cy_chan c);
void      cy_chan_dispose(cy_chan c);

#endif /* CLASSYC_CYFIBER_H */

/* ══════════════════════════ Implementation ═══════════════════════════════
   Host-compiled only (gcc).  ClassyC translation units never define
   CYFIBER_IMPLEMENTATION, so they only see the declarations above. */
#ifdef CYFIBER_IMPLEMENTATION
#ifndef CLASSYC_CYFIBER_IMPL
#define CLASSYC_CYFIBER_IMPL

/* Host-compiled with gcc/clang: real __thread / _Thread_local works. */
#define MINICORO_IMPL
#include "minicoro.h"
#define CCHAN_IMPLEMENTATION
#include "cchan.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define CY_FIBER_STACK (64 * 1024)
#define CY_MAX_WORKERS 64
#define CY_GO_MAX_ARGS 8

struct cy_fiber {
    mco_coro *co;
    int       worker;   /* pinned worker index (worker mode) */
};

struct cy_chan_impl {
    cchan_t *c;          /* buffered path (capacity > 0) */
    /* Unbuffered rendezvous path (capacity == 0).  cchan's try_recv on an
       empty unbuffered channel does not register a receiver, so try_send can
       never succeed — a pure park-by-yield loop over cchan deadlocks for
       fiber↔fiber peers on one worker.  We therefore implement rendezvous
       ourselves: a 1-slot mailbox with a mutex + state flags, parked by
       cy_yield (never by OS-thread blocking). */
    int      cap0;
    pthread_mutex_t lock;
    int      state;      /* 0 = empty, 1 = full (value posted in slot) */
    int      closed;
    int      msg_size;
    unsigned char slot[1]; /* msg_size bytes (struct is over-allocated) */
};

/* ── Global scheduler state ─────────────────────────────────────────────── */
static pthread_mutex_t g_cy_lock = PTHREAD_MUTEX_INITIALIZER;
static cy_fiber      **g_cy_fibers = NULL;   /* dynamic table of live fibers */
static int             g_cy_nfibers = 0;
static int             g_cy_capfibers = 0;
static int             g_cy_mode = 0;        /* 0 uninit, 1 single-thread, 2 workers */
static int             g_cy_nworkers = 0;
static pthread_t       g_cy_threads[CY_MAX_WORKERS];
static volatile long   g_cy_outstanding = 0; /* live fiber count */
static volatile int    g_cy_stop = 0;        /* workers may exit when drained */
static volatile long   g_cy_next_worker = 0; /* round-robin spawn assignment */
static int             g_cy_cursor = 0;      /* single-mode round-robin cursor */
static int             g_cy_started = 0;     /* any fiber resumed (pins mode 1) */

/* ── Fiber entry ────────────────────────────────────────────────────────── */
typedef struct {
    cy_fiber_fn fn;
    void       *arg;
} cy_entry_ctx;

static void cy_fiber_entry (mco_coro *co) {
    cy_entry_ctx *cx = (cy_entry_ctx *) mco_get_user_data (co);
    cy_fiber_fn fn = cx->fn;
    void *arg = cx->arg;
    free (cx);
    fn (arg);
}

/* ── Table helpers (caller holds g_cy_lock) ─────────────────────────────── */
static void cy_fiber_reap_locked (int i) {
    cy_fiber *f = g_cy_fibers[i];
    mco_destroy (f->co);
    free (f);
    g_cy_fibers[i] = g_cy_fibers[--g_cy_nfibers];
    __atomic_fetch_sub (&g_cy_outstanding, 1, __ATOMIC_SEQ_CST);
}

static int cy_worker_has_fibers_locked (long w) {
    for (int i = 0; i < g_cy_nfibers; i++)
        if (g_cy_fibers[i]->worker == w) return 1;
    return 0;
}

/* ── Worker loop (pthread mode): round-robin sweep of own pinned fibers ── */
static void *cy_worker_main (void *arg) {
    long w = (long) arg;
    int cursor = 0;
    for (;;) {
        mco_coro *co = NULL;
        int stop, none_mine;
        pthread_mutex_lock (&g_cy_lock);
        int n = g_cy_nfibers;
        if (n > 0) {
            if (cursor >= n) cursor = 0;
            for (int k = 0; k < n; k++) {
                int i = (cursor + k) % n;
                cy_fiber *f = g_cy_fibers[i];
                if (f->worker != w) continue;
                if (mco_status (f->co) == MCO_SUSPENDED) {
                    co = f->co;
                    cursor = i + 1;
                    break;
                }
            }
        }
        for (int i = 0; i < g_cy_nfibers; i++) {   /* reap dead fibers of mine */
            cy_fiber *f = g_cy_fibers[i];
            if (f->worker == w && mco_status (f->co) == MCO_DEAD) {
                cy_fiber_reap_locked (i);
                i--;
            }
        }
        stop = g_cy_stop;
        none_mine = stop && !cy_worker_has_fibers_locked (w);
        pthread_mutex_unlock (&g_cy_lock);
        if (none_mine) break;
        if (co != NULL) {
            mco_resume (co);        /* fiber may spawn/yield — no lock held */
            continue;
        }
        usleep (200);
    }
    return NULL;
}

/* ── Single-thread scheduler step: reap dead, resume one (round-robin) ──── */
static int cy_sched_step (void) {
    mco_coro *co = NULL;
    pthread_mutex_lock (&g_cy_lock);
    int n = g_cy_nfibers;
    if (n > 0) {
        if (g_cy_cursor >= n) g_cy_cursor = 0;
        for (int k = 0; k < n; k++) {
            int i = (g_cy_cursor + k) % n;
            cy_fiber *f = g_cy_fibers[i];
            if (mco_status (f->co) == MCO_SUSPENDED) {
                co = f->co;
                g_cy_cursor = i + 1;
                break;
            }
        }
    }
    for (int i = 0; i < g_cy_nfibers; i++) {
        if (mco_status (g_cy_fibers[i]->co) == MCO_DEAD) {
            cy_fiber_reap_locked (i);
            i--;
        }
    }
    pthread_mutex_unlock (&g_cy_lock);
    if (co != NULL) {
        g_cy_started = 1;
        mco_resume (co);
        return 1;
    }
    return 0;
}

/* ── Scheduler public API ───────────────────────────────────────────────── */
void cy_sched_init (int n_workers) {
    int start_workers = 0;
    pthread_mutex_lock (&g_cy_lock);
    if (g_cy_mode == 1 && n_workers > 1 && !g_cy_started) {
        /* Upgrade lazy single-thread mode before any fiber ran on the
           caller thread (minicoro forbids cross-thread resume afterwards). */
        g_cy_mode = 0;
    }
    if (g_cy_mode != 0) { pthread_mutex_unlock (&g_cy_lock); return; }
    if (n_workers <= 1) {
        g_cy_mode = 1;
        g_cy_nworkers = 1;
        pthread_mutex_unlock (&g_cy_lock);
        return;
    }
    if (n_workers > CY_MAX_WORKERS) n_workers = CY_MAX_WORKERS;
    g_cy_mode = 2;
    g_cy_nworkers = n_workers;
    g_cy_stop = 0;
    for (int i = 0; i < g_cy_nfibers; i++)    /* pin pending fibers round-robin */
        g_cy_fibers[i]->worker = i % n_workers;
    start_workers = 1;
    pthread_mutex_unlock (&g_cy_lock);
    if (start_workers)
        for (long w = 0; w < n_workers; w++)
            pthread_create (&g_cy_threads[w], NULL, cy_worker_main, (void *) w);
}

void cy_sched_run (void) {
    if (g_cy_mode == 0) cy_sched_init (1);
    if (g_cy_mode == 1) {
        while (__atomic_load_n (&g_cy_outstanding, __ATOMIC_SEQ_CST) != 0)
            cy_sched_step ();
        return;
    }
    while (__atomic_load_n (&g_cy_outstanding, __ATOMIC_SEQ_CST) != 0)
        usleep (200);
    g_cy_stop = 1;
    for (int w = 0; w < g_cy_nworkers; w++)
        pthread_join (g_cy_threads[w], NULL);
}

void cy_sched_shutdown (void) {
    if (g_cy_mode == 2 && !g_cy_stop) {
        g_cy_stop = 1;
        for (int w = 0; w < g_cy_nworkers; w++)
            pthread_join (g_cy_threads[w], NULL);
    }
    pthread_mutex_lock (&g_cy_lock);
    for (int i = 0; i < g_cy_nfibers; i++) {
        cy_fiber *f = g_cy_fibers[i];
        void *cx = mco_get_user_data (f->co);
        if (cx != NULL) free (cx);
        mco_destroy (f->co);
        free (f);
    }
    free (g_cy_fibers);
    g_cy_fibers = NULL;
    g_cy_nfibers = g_cy_capfibers = 0;
    g_cy_mode = g_cy_nworkers = 0;
    g_cy_outstanding = 0;
    g_cy_stop = 0;
    g_cy_next_worker = 0;
    g_cy_cursor = 0;
    g_cy_started = 0;
    pthread_mutex_unlock (&g_cy_lock);
}

void add_scheduler (int threads) {
    cy_sched_init (threads);
    cy_sched_run ();
}

void add_schedular (int threads) { add_scheduler (threads); }

/* ── Fibers ─────────────────────────────────────────────────────────────── */
cy_fiber *cy_spawn (cy_fiber_fn fn, void *arg) {
    cy_entry_ctx *cx;
    cy_fiber *f;
    mco_desc desc;

    if (g_cy_mode == 0) cy_sched_init (1);   /* lazy single-thread mode */
    cx = (cy_entry_ctx *) malloc (sizeof (cy_entry_ctx));
    f = (cy_fiber *) calloc (1, sizeof (cy_fiber));
    if (cx == NULL || f == NULL) { free (cx); free (f); return NULL; }
    cx->fn = fn;
    cx->arg = arg;
    desc = mco_desc_init (cy_fiber_entry, CY_FIBER_STACK);
    desc.user_data = cx;
    if (mco_create (&f->co, &desc) != MCO_SUCCESS) {
        free (cx);
        free (f);
        return NULL;
    }
    pthread_mutex_lock (&g_cy_lock);
    if (g_cy_mode == 2)
        f->worker
          = (int) (__atomic_fetch_add (&g_cy_next_worker, 1, __ATOMIC_SEQ_CST) % g_cy_nworkers);
    if (g_cy_nfibers == g_cy_capfibers) {
        int ncap = g_cy_capfibers > 0 ? g_cy_capfibers * 2 : 64;
        cy_fiber **nf = (cy_fiber **) realloc (g_cy_fibers, ncap * sizeof (cy_fiber *));
        if (nf == NULL) {
            pthread_mutex_unlock (&g_cy_lock);
            mco_destroy (f->co);
            free (cx);
            free (f);
            return NULL;
        }
        g_cy_fibers = nf;
        g_cy_capfibers = ncap;
    }
    g_cy_fibers[g_cy_nfibers++] = f;
    __atomic_fetch_add (&g_cy_outstanding, 1, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock (&g_cy_lock);
    return f;
}

void cy_yield (void) {
    mco_coro *co = mco_running ();
    if (co != NULL) {
        mco_yield (co);
        return;
    }
    /* Outside a fiber (e.g. main thread parked on a channel): keep the
       single-thread scheduler moving; in worker mode the workers pump
       themselves, so just avoid a hot spin. */
    if (g_cy_mode == 1)
        cy_sched_step ();
    else
        usleep (100);
}

cy_fiber *cy_self (void) {
    mco_coro *running = mco_running ();
    if (running == NULL) return NULL;
    pthread_mutex_lock (&g_cy_lock);
    for (int i = 0; i < g_cy_nfibers; i++)
        if (g_cy_fibers[i]->co == running) {
            cy_fiber *f = g_cy_fibers[i];
            pthread_mutex_unlock (&g_cy_lock);
            return f;
        }
    pthread_mutex_unlock (&g_cy_lock);
    return NULL;
}

long cy_fiber_outstanding (void) {
    return __atomic_load_n (&g_cy_outstanding, __ATOMIC_SEQ_CST);
}

static long cy_now_ms (void) {
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void cy_sleep_ms (int ms) {
    if (mco_running () != NULL) {
        long deadline = cy_now_ms () + ms;
        while (cy_now_ms () < deadline)
            cy_yield ();
    } else {
        usleep ((unsigned int) ms * 1000u);
    }
}

/* ── `go` trampoline: fixed-arity value pack ────────────────────────────── */
typedef struct {
    void *fn;
    long  nargs;
    long  a[CY_GO_MAX_ARGS];
} cy_go_pack;

static void cy_go_tramp (void *vp) {
    cy_go_pack *pk = (cy_go_pack *) vp;
    switch (pk->nargs) {
    case 0: ((void (*) (void)) pk->fn) (); break;
    case 1: ((void (*) (long)) pk->fn) (pk->a[0]); break;
    case 2: ((void (*) (long, long)) pk->fn) (pk->a[0], pk->a[1]); break;
    case 3: ((void (*) (long, long, long)) pk->fn) (pk->a[0], pk->a[1], pk->a[2]); break;
    case 4: ((void (*) (long, long, long, long)) pk->fn) (pk->a[0], pk->a[1], pk->a[2], pk->a[3]); break;
    case 5: ((void (*) (long, long, long, long, long)) pk->fn)
        (pk->a[0], pk->a[1], pk->a[2], pk->a[3], pk->a[4]); break;
    case 6: ((void (*) (long, long, long, long, long, long)) pk->fn)
        (pk->a[0], pk->a[1], pk->a[2], pk->a[3], pk->a[4], pk->a[5]); break;
    case 7: ((void (*) (long, long, long, long, long, long, long)) pk->fn)
        (pk->a[0], pk->a[1], pk->a[2], pk->a[3], pk->a[4], pk->a[5], pk->a[6]); break;
    default: ((void (*) (long, long, long, long, long, long, long, long)) pk->fn)
        (pk->a[0], pk->a[1], pk->a[2], pk->a[3], pk->a[4], pk->a[5], pk->a[6], pk->a[7]); break;
    }
    free (pk);
}

void cy_spawn8 (void *fn, long nargs,
                long a0, long a1, long a2, long a3,
                long a4, long a5, long a6, long a7) {
    cy_go_pack *pk = (cy_go_pack *) malloc (sizeof (cy_go_pack));
    if (pk == NULL) return;
    pk->fn = fn;
    pk->nargs = nargs < 0 ? 0 : (nargs > CY_GO_MAX_ARGS ? CY_GO_MAX_ARGS : nargs);
    pk->a[0] = a0; pk->a[1] = a1; pk->a[2] = a2; pk->a[3] = a3;
    pk->a[4] = a4; pk->a[5] = a5; pk->a[6] = a6; pk->a[7] = a7;
    if (cy_spawn (cy_go_tramp, pk) == NULL) free (pk);
}

/* ── Channels ───────────────────────────────────────────────────────────── */
cy_chan cy_chan_create (int capacity, int msg_size) {
    cy_chan ch;
    if (capacity < 0) capacity = 0;
    if (capacity > 65535) capacity = 65535;
    if (msg_size <= 0) msg_size = 1;
    if (msg_size > 65535) msg_size = 65535;
    ch = (cy_chan) malloc (sizeof (struct cy_chan_impl) + (size_t) msg_size);
    if (ch == NULL) return NULL;
    ch->cap0 = capacity == 0;
    ch->msg_size = msg_size;
    ch->state = 0;
    ch->closed = 0;
    if (ch->cap0) {
        pthread_mutex_init (&ch->lock, NULL);
        ch->c = NULL;
    } else {
        ch->c = cchan_create ((unsigned short) capacity, (unsigned short) msg_size);
        if (ch->c == NULL) { free (ch); return NULL; }
    }
    return ch;
}

/* Rendezvous helpers (cap0 path).  True rendezvous for the park ops: the
   sender posts the value and then waits for the receiver to take it. */
static int cy_chan0_send_park (cy_chan ch, const void *msg) {
    pthread_mutex_lock (&ch->lock);
    for (;;) {
        if (ch->closed) { pthread_mutex_unlock (&ch->lock); return 0; }
        if (ch->state == 0) break;             /* slot free: post */
        pthread_mutex_unlock (&ch->lock);
        cy_yield ();
        pthread_mutex_lock (&ch->lock);
    }
    memcpy (ch->slot, msg, (size_t) ch->msg_size);
    ch->state = 1;
    pthread_mutex_unlock (&ch->lock);
    for (;;) {                                  /* wait for pickup */
        pthread_mutex_lock (&ch->lock);
        if (ch->state == 0) { pthread_mutex_unlock (&ch->lock); return 1; }
        if (ch->closed) { pthread_mutex_unlock (&ch->lock); return 0; }
        pthread_mutex_unlock (&ch->lock);
        cy_yield ();
    }
}

static int cy_chan0_recv_park (cy_chan ch, void *msg) {
    for (;;) {
        pthread_mutex_lock (&ch->lock);
        if (ch->state == 1) {
            memcpy (msg, ch->slot, (size_t) ch->msg_size);
            ch->state = 0;
            pthread_mutex_unlock (&ch->lock);
            return 1;
        }
        if (ch->closed) { pthread_mutex_unlock (&ch->lock); return 0; }
        pthread_mutex_unlock (&ch->lock);
        cy_yield ();
    }
}

/* try_* on a rendezvous channel behave as a 1-deep mailbox: the try succeeds
   iff the slot is free (send) / full (recv) right now — no pickup wait. */
static int cy_chan0_try_send (cy_chan ch, const void *msg) {
    int rc = 0;
    pthread_mutex_lock (&ch->lock);
    if (ch->closed) rc = -1;
    else if (ch->state == 0) {
        memcpy (ch->slot, msg, (size_t) ch->msg_size);
        ch->state = 1;
        rc = 1;
    }
    pthread_mutex_unlock (&ch->lock);
    return rc;
}

static int cy_chan0_try_recv (cy_chan ch, void *msg) {
    int rc = 0;
    pthread_mutex_lock (&ch->lock);
    if (ch->state == 1) {
        memcpy (msg, ch->slot, (size_t) ch->msg_size);
        ch->state = 0;
        rc = 1;
    } else if (ch->closed) {
        rc = -1;
    }
    pthread_mutex_unlock (&ch->lock);
    return rc;
}

int cy_chan_send_park (cy_chan ch, const void *msg) {
    if (ch == NULL) return 0;
    if (ch->cap0) return cy_chan0_send_park (ch, msg);
    for (;;) {
        int rc = cchan_try_send (ch->c, msg);
        if (rc != 0) return rc > 0;   /* 1 sent, -1 closed */
        cy_yield ();
    }
}

int cy_chan_recv_park (cy_chan ch, void *msg) {
    if (ch == NULL) return 0;
    if (ch->cap0) return cy_chan0_recv_park (ch, msg);
    for (;;) {
        int rc = cchan_try_recv (ch->c, msg);
        if (rc != 0) return rc > 0;   /* 1 got, -1 closed+drained */
        cy_yield ();
    }
}

int cy_chan_try_send (cy_chan ch, const void *msg) {
    if (ch == NULL) return -1;
    return ch->cap0 ? cy_chan0_try_send (ch, msg) : cchan_try_send (ch->c, msg);
}

int cy_chan_try_recv (cy_chan ch, void *msg) {
    if (ch == NULL) return -1;
    return ch->cap0 ? cy_chan0_try_recv (ch, msg) : cchan_try_recv (ch->c, msg);
}

int cy_chan_send_timeout (cy_chan ch, const void *msg, int ms) {
    long deadline;
    if (ch == NULL) return -1;
    if (ms <= 0) return cy_chan_try_send (ch, msg);
    deadline = cy_now_ms () + ms;
    for (;;) {
        int rc = cy_chan_try_send (ch, msg);
        if (rc != 0) return rc;
        if (cy_now_ms () >= deadline) return 0;
        cy_yield ();
    }
}

int cy_chan_recv_timeout (cy_chan ch, void *msg, int ms) {
    long deadline;
    if (ch == NULL) return -1;
    if (ms <= 0) return cy_chan_try_recv (ch, msg);
    deadline = cy_now_ms () + ms;
    for (;;) {
        int rc = cy_chan_try_recv (ch, msg);
        if (rc != 0) return rc;
        if (cy_now_ms () >= deadline) return 0;
        cy_yield ();
    }
}

int cy_chan_close (cy_chan ch) {
    int already;
    if (ch == NULL) return 0;
    if (!ch->cap0) return cchan_close (ch->c) == 0 ? 1 : 0;   /* cchan: 0 = first close */
    pthread_mutex_lock (&ch->lock);
    already = ch->closed;
    ch->closed = 1;
    pthread_mutex_unlock (&ch->lock);
    return already ? 0 : 1;
}

int cy_chan_is_closed (cy_chan ch) {
    int cl;
    if (ch == NULL) return 1;
    if (!ch->cap0) return cchan_is_closed (ch->c);
    pthread_mutex_lock (&ch->lock);
    cl = ch->closed;
    pthread_mutex_unlock (&ch->lock);
    return cl;
}

int cy_chan_size (cy_chan ch) {
    int st;
    if (ch == NULL) return 0;
    if (!ch->cap0) return cchan_size (ch->c);
    pthread_mutex_lock (&ch->lock);
    st = ch->state;
    pthread_mutex_unlock (&ch->lock);
    return st;
}

int cy_chan_capacity (cy_chan ch) {
    return ch == NULL ? 0 : (ch->cap0 ? 0 : cchan_capacity (ch->c));
}

void cy_chan_dispose (cy_chan ch) {
    if (ch == NULL) return;
    if (ch->cap0)
        pthread_mutex_destroy (&ch->lock);
    else
        cchan_dispose (ch->c);
    free (ch);
}

#endif /* CLASSYC_CYFIBER_IMPL */
#endif /* CYFIBER_IMPLEMENTATION */
