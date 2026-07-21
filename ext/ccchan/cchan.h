/*
 * cchan — professional header-only CSP / Go-style channels for C
 *
 * Copyright 2023 Rochus Keller <mailto:me@rochus-keller.ch>
 * Copyright 2026 Roger Davenport & contributors (hardening, rename, header-only redesign)
 *
 * License: LGPL-2.1-or-later OR MPL-2.0 (see LICENSE.LGPLv21 / LICENSE.MPLv2)
 *
 * ============================================================================
 * USAGE
 * ============================================================================
 *
 *   // Exactly one translation unit:
 *   #define CCHAN_IMPLEMENTATION
 *   #include "cchan.h"
 *
 *   // Every other TU:
 *   #include "cchan.h"
 *
 * Link with -lpthread. Channels only — create threads yourself
 * (pthread_create, std::thread, ClassyC runtime, …).
 *
 * Features:
 *   - buffered and unbuffered (rendezvous) channels
 *   - close with drain-then-EOF receive semantics (Go-like)
 *   - blocking + non-blocking send/recv
 *   - budgeted try (spin/yield) + wall-clock timeout send/recv
 *   - blocking + non-blocking + timed select (fair among ready cases)
 *   - reference-counted lifetime (retain / release)
 *   - cchan_size / cchan_capacity
 *   - typed helpers for int32/int64/double/buf
 *
 * Inspired by Go channels and by tylertreat/chan; implementation is original
 * and value-oriented (messages are memcpy'd, not void* identity).
 *
 * Anti-deadlock guidance (multi-hop / ClassyC codegen):
 *   - Prefer timeout/budget on hot paths; never park forever waiting for a
 *     reply that only arrives after you exit.
 *   - Orchestrate sessions as: signal stop → close channels → join workers.
 *   - Never block-send on a full output while you are the only consumer of
 *     an upstream that fills because of that block (circular wait).
 *   See API.md "Lifecycle and anti-deadlock patterns".
 */

#ifndef CCHAN_H
#define CCHAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>     /* size_t */
#include <stdint.h>     /* int32_t, int64_t */

#if defined(_WIN32) && defined(CCHAN_BUILD_DLL)
#  define CCHAN_API __declspec(dllexport)
#elif defined(_WIN32) && defined(CCHAN_USE_DLL)
#  define CCHAN_API __declspec(dllimport)
#else
#  define CCHAN_API
#endif

typedef struct cchan cchan_t;

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

/*
 * capacity == 0 -> unbuffered rendezvous channel
 * capacity  > 0 -> bounded ring buffer of that many messages
 * msg_size  == 0 -> treated as 1
 * Returns NULL on OOM. Initial refcount is 1.
 */
CCHAN_API cchan_t* cchan_create(unsigned short capacity, unsigned short msg_size);

/* Refcount. dispose is an alias for release. Freed on last release. */
CCHAN_API void cchan_retain(cchan_t* c);
CCHAN_API void cchan_release(cchan_t* c);
CCHAN_API void cchan_dispose(cchan_t* c);

/*
 * Close the channel. Idempotent for waking waiters; returns 0 on first close,
 * -1 if already closed. After close: send fails; receive drains then fails.
 */
CCHAN_API int  cchan_close(cchan_t* c);
CCHAN_API int  cchan_is_closed(cchan_t* c);   /* 1 closed/NULL, else 0 */

/* Buffered queue depth (0 if unbuffered or empty). */
CCHAN_API int  cchan_size(cchan_t* c);
/* Configured capacity (0 if unbuffered). */
CCHAN_API int  cchan_capacity(cchan_t* c);
/* Message size in bytes. */
CCHAN_API int  cchan_msg_size(cchan_t* c);

/* -------------------------------------------------------------------------- */
/* Send / receive                                                             */
/* -------------------------------------------------------------------------- */

/*
 * Blocking. Returns 1 if a message was transferred, 0 if closed (no transfer).
 * data must address at least msg_size bytes.
 */
CCHAN_API int cchan_send(cchan_t* c, const void* data);
CCHAN_API int cchan_recv(cchan_t* c, void* data);

/* Non-blocking: 1 transferred, 0 would-block, -1 closed. */
CCHAN_API int cchan_try_send(cchan_t* c, const void* data);
CCHAN_API int cchan_try_recv(cchan_t* c, void* data);

/*
 * Budgeted try: repeat try_send/try_recv up to `spins` attempts with light
 * yielding. Preferred hot-path primitive for multi-hop pipelines so a full
 * downstream buffer never parks the engine forever.
 *
 * spins == 0  → single try (same as try_*).
 * Returns: 1 transferred, 0 budget exhausted (would block), -1 closed/invalid.
 */
CCHAN_API int cchan_send_budget(cchan_t* c, const void* data, unsigned spins);
CCHAN_API int cchan_recv_budget(cchan_t* c, void* data, unsigned spins);

/*
 * Wall-clock bounded ops (milliseconds, CLOCK_REALTIME based).
 * timeout_ms == 0 behaves like try_* (no sleep).
 *
 * Returns: 1 transferred, 0 timed out (no transfer), -1 closed/invalid.
 * On closed empty receive, destination is zeroed (same as blocking recv).
 */
CCHAN_API int cchan_send_timeout(cchan_t* c, const void* data, unsigned timeout_ms);
CCHAN_API int cchan_recv_timeout(cchan_t* c, void* data, unsigned timeout_ms);

/* -------------------------------------------------------------------------- */
/* Select                                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Blocking select over receive and/or send cases.
 * Returns index in logical array receivers|senders, or -1 if every listed
 * channel is closed with nothing left to deliver.
 */
CCHAN_API int cchan_select(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                           cchan_t** send_chans, void** send_bufs, unsigned send_count);

/* Non-blocking select. -1 => none ready (or all closed). */
CCHAN_API int cchan_nb_select(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                              cchan_t** send_chans, void** send_bufs, unsigned send_count);

/*
 * Timed select. Same indexing as cchan_select.
 * timeout_ms == 0 behaves like cchan_nb_select for the ready check, except
 * all-closed is distinguished.
 *
 * Returns:
 *   >= 0  index of the case that ran
 *   -1    timed out (at least one listed channel still live / busy)
 *   -2    every listed channel is closed with nothing left, empty list, or OOM
 */
CCHAN_API int cchan_select_timeout(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                                   cchan_t** send_chans, void** send_bufs, unsigned send_count,
                                   unsigned timeout_ms);

/* -------------------------------------------------------------------------- */
/* Typed helpers (fixed-size convenience; channel msg_size must match)        */
/* -------------------------------------------------------------------------- */

CCHAN_API int cchan_send_i32(cchan_t* c, int32_t v);
CCHAN_API int cchan_recv_i32(cchan_t* c, int32_t* out);
CCHAN_API int cchan_send_i64(cchan_t* c, int64_t v);
CCHAN_API int cchan_recv_i64(cchan_t* c, int64_t* out);
CCHAN_API int cchan_send_double(cchan_t* c, double v);
CCHAN_API int cchan_recv_double(cchan_t* c, double* out);

/* Copy exactly len bytes (len must equal channel msg_size). */
CCHAN_API int cchan_send_buf(cchan_t* c, const void* buf, size_t len);
CCHAN_API int cchan_recv_buf(cchan_t* c, void* buf, size_t len);

/* Sleep the calling thread (milliseconds). */
CCHAN_API void cchan_sleep(unsigned milliseconds);

#ifdef __cplusplus
}
#endif

/* ========================================================================== */
/* IMPLEMENTATION                                                             */
/* ========================================================================== */
#ifdef CCHAN_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#if defined(_WIN32)
#  error "cchan: Win32 backend not in this revision (POSIX/pthreads required)"
#else
#  include <pthread.h>
#  include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Internals                                                                  */
/* -------------------------------------------------------------------------- */

enum { CCHAN_OBS_SLOTS = 4 };

/* Select waiters: store mutex+cond so signal can avoid lost-wakeup races. */
typedef struct cchan_waiter {
    pthread_mutex_t* mu;
    pthread_cond_t*  cv;
} cchan_waiter;

typedef struct cchan_obs {
    cchan_waiter        slot[CCHAN_OBS_SLOTS];
    struct cchan_obs*   next;
} cchan_obs;

/*
 * Unbuffered rendezvous (under c->mu):
 *   phase 0 — idle
 *   phase 1 — one side waiting; want_send says required peer role
 *   phase 2 — transfer done; first side resets to 0
 */
struct cchan {
    pthread_mutex_t mu;
    pthread_cond_t  can_send;
    pthread_cond_t  can_recv;
    pthread_mutex_t obs_mu;
    cchan_obs       obs;

    unsigned        refcount;
    unsigned short  msg_size;
    unsigned short  capacity;     /* 0 => unbuffered */
    unsigned short  count;
    unsigned short  ridx;
    unsigned short  widx;
    unsigned short  closed;
    unsigned short  unbuffered;
    unsigned short  phase;
    unsigned short  want_send;    /* phase1: 1 need sender, 0 need receiver */
    void*           rendezvous;

    unsigned char   data[1];
};

#define CCHAN_HDR (offsetof(cchan_t, data))

#ifndef CCHAN_LOG_ERRORS
#  define CCHAN_LOG_ERRORS 1
#endif

#if CCHAN_LOG_ERRORS
/* GNU empty ##__VA_ARGS__ requires ClassyC/c2mir GCC-compat macro paste
 * (see MysticalUnicat:mir preprocessor fixes in find_args / token_concat /
 * process_replacement). */
#  define CCHAN_ERR(fmt, ...) \
      fprintf(stderr, "cchan: " fmt " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__)
#else
#  define CCHAN_ERR(fmt, ...) ((void)0)
#endif

#define CCHAN_OK(call) do { \
    int _rc = (call); \
    if (_rc != 0) CCHAN_ERR("call failed rc=%d errno=%d", _rc, errno); \
} while (0)

/* ---- observers ----------------------------------------------------------- */

static void cchan_obs_signal_all(cchan_t* c)
{
    cchan_obs* s;
    int i;
    CCHAN_OK(pthread_mutex_lock(&c->obs_mu));
    for (s = &c->obs; s; s = s->next) {
        for (i = 0; i < CCHAN_OBS_SLOTS; i++) {
            cchan_waiter* w = &s->slot[i];
            if (w->cv && w->mu) {
                /* Lock waiter mutex then signal — closes the check/wait race. */
                CCHAN_OK(pthread_mutex_lock(w->mu));
                CCHAN_OK(pthread_cond_signal(w->cv));
                CCHAN_OK(pthread_mutex_unlock(w->mu));
            }
        }
    }
    CCHAN_OK(pthread_mutex_unlock(&c->obs_mu));
}

static void cchan_obs_add(cchan_t* c, pthread_mutex_t* mu, pthread_cond_t* cv)
{
    cchan_obs* s;
    int i;
    CCHAN_OK(pthread_mutex_lock(&c->obs_mu));
    s = &c->obs;
    for (;;) {
        for (i = 0; i < CCHAN_OBS_SLOTS; i++) {
            if (s->slot[i].cv == 0) {
                s->slot[i].mu = mu;
                s->slot[i].cv = cv;
                CCHAN_OK(pthread_mutex_unlock(&c->obs_mu));
                return;
            }
        }
        if (!s->next) {
            s->next = (cchan_obs*)calloc(1, sizeof(cchan_obs));
            if (!s->next) {
                CCHAN_OK(pthread_mutex_unlock(&c->obs_mu));
                CCHAN_ERR("OOM adding select observer");
                return;
            }
        }
        s = s->next;
    }
}

static void cchan_obs_remove(cchan_t* c, pthread_cond_t* cv)
{
    cchan_obs* s;
    int i;
    CCHAN_OK(pthread_mutex_lock(&c->obs_mu));
    for (s = &c->obs; s; s = s->next) {
        for (i = 0; i < CCHAN_OBS_SLOTS; i++) {
            if (s->slot[i].cv == cv) {
                s->slot[i].mu = 0;
                s->slot[i].cv = 0;
                CCHAN_OK(pthread_mutex_unlock(&c->obs_mu));
                return;
            }
        }
    }
    CCHAN_OK(pthread_mutex_unlock(&c->obs_mu));
}

/* ---- buffer helpers ------------------------------------------------------ */

static int cchan_full(const cchan_t* c)
{
    return c->capacity > 0 && c->count == c->capacity;
}

static int cchan_empty(const cchan_t* c)
{
    return c->count == 0;
}

static void cchan_buf_write(cchan_t* c, const void* src)
{
    memcpy(c->data + (size_t)c->widx * c->msg_size, src, c->msg_size);
    c->widx = (unsigned short)((c->widx + 1u) % c->capacity);
    c->count++;
}

static void cchan_buf_read(cchan_t* c, void* dst)
{
    memcpy(dst, c->data + (size_t)c->ridx * c->msg_size, c->msg_size);
    c->ridx = (unsigned short)((c->ridx + 1u) % c->capacity);
    c->count--;
}

static void cchan_wake_locked(cchan_t* c)
{
    CCHAN_OK(pthread_cond_broadcast(&c->can_send));
    CCHAN_OK(pthread_cond_broadcast(&c->can_recv));
}

/* Absolute deadline = now + timeout_ms (CLOCK_REALTIME; matches pthread_cond). */
static void cchan_deadline_ms(unsigned timeout_ms, struct timespec* abs)
{
    clock_gettime(CLOCK_REALTIME, abs);
    abs->tv_sec  += (time_t)(timeout_ms / 1000u);
    abs->tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (abs->tv_nsec >= 1000000000L) {
        abs->tv_sec  += 1;
        abs->tv_nsec -= 1000000000L;
    }
}

/* Light progressive yield for budgeted try loops (nanosleep, no cchan_sleep dep). */
static void cchan_budget_yield(unsigned attempt)
{
    if ((attempt & 63u) == 63u) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 0; /* sched_yield equivalent via nanosleep(0) */
        (void)nanosleep(&ts, 0);
    }
}

static void cchan_destroy(cchan_t* c)
{
    cchan_obs* s;
    CCHAN_OK(pthread_cond_destroy(&c->can_send));
    CCHAN_OK(pthread_cond_destroy(&c->can_recv));
    CCHAN_OK(pthread_mutex_destroy(&c->obs_mu));
    CCHAN_OK(pthread_mutex_destroy(&c->mu));
    s = c->obs.next;
    while (s) {
        cchan_obs* n = s->next;
        free(s);
        s = n;
    }
    free(c);
}

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

CCHAN_API cchan_t* cchan_create(unsigned short capacity, unsigned short msg_size)
{
    size_t bytes;
    cchan_t* c;

    if (msg_size == 0)
        msg_size = 1;

    bytes = CCHAN_HDR + (size_t)capacity * (size_t)msg_size;
    if (capacity == 0)
        bytes = CCHAN_HDR + 1;

    c = (cchan_t*)calloc(1, bytes);
    if (!c)
        return 0;

    c->refcount   = 1;
    c->msg_size   = msg_size;
    c->capacity   = capacity;
    c->unbuffered = (capacity == 0) ? 1u : 0u;

    if (pthread_mutex_init(&c->mu, 0) != 0) { free(c); return 0; }
    if (pthread_mutex_init(&c->obs_mu, 0) != 0) {
        pthread_mutex_destroy(&c->mu); free(c); return 0;
    }
    if (pthread_cond_init(&c->can_send, 0) != 0) {
        pthread_mutex_destroy(&c->obs_mu);
        pthread_mutex_destroy(&c->mu); free(c); return 0;
    }
    if (pthread_cond_init(&c->can_recv, 0) != 0) {
        pthread_cond_destroy(&c->can_send);
        pthread_mutex_destroy(&c->obs_mu);
        pthread_mutex_destroy(&c->mu); free(c); return 0;
    }
    return c;
}

CCHAN_API void cchan_retain(cchan_t* c)
{
    if (!c) return;
    CCHAN_OK(pthread_mutex_lock(&c->mu));
    c->refcount++;
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
}

CCHAN_API void cchan_release(cchan_t* c)
{
    unsigned left;
    if (!c) return;

    CCHAN_OK(pthread_mutex_lock(&c->mu));
    if (c->refcount == 0) {
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return;
    }
    left = --c->refcount;
    if (left == 0) {
        c->closed = 1;
        cchan_wake_locked(c);
    }
    CCHAN_OK(pthread_mutex_unlock(&c->mu));

    if (left == 0) {
        cchan_obs_signal_all(c);
        cchan_destroy(c);
    }
}

CCHAN_API void cchan_dispose(cchan_t* c)
{
    cchan_release(c);
}

CCHAN_API int cchan_close(cchan_t* c)
{
    int first = 0;
    if (!c) return -1;

    CCHAN_OK(pthread_mutex_lock(&c->mu));
    if (!c->closed) {
        c->closed = 1;
        first = 1;
        if (c->unbuffered) {
            c->phase = 0;
            c->rendezvous = 0;
        }
        cchan_wake_locked(c);
    }
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    /* Always kick observers: waiters may have registered after a prior close. */
    cchan_obs_signal_all(c);
    return first ? 0 : -1;
}

CCHAN_API int cchan_is_closed(cchan_t* c)
{
    int v;
    if (!c) return 1;
    CCHAN_OK(pthread_mutex_lock(&c->mu));
    v = c->closed ? 1 : 0;
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    return v;
}

CCHAN_API int cchan_size(cchan_t* c)
{
    int n;
    if (!c) return 0;
    CCHAN_OK(pthread_mutex_lock(&c->mu));
    n = c->unbuffered ? 0 : (int)c->count;
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    return n;
}

CCHAN_API int cchan_capacity(cchan_t* c)
{
    return c ? (int)c->capacity : 0;
}

CCHAN_API int cchan_msg_size(cchan_t* c)
{
    return c ? (int)c->msg_size : 0;
}

/* -------------------------------------------------------------------------- */
/* Unbuffered rendezvous                                                      */
/* -------------------------------------------------------------------------- */

/*
 * Unbuffered rendezvous.
 * c->mu held on entry; always released before return.
 * deadline == NULL  → wait forever (blocking send/recv)
 * deadline != NULL  → pthread_cond_timedwait; may return 0 on timeout
 *
 * Returns: 1 transferred, 0 closed or timed out (no transfer).
 * On closed empty recv, zeros data. On timeout, leaves data untouched for
 * senders; zeros data for receivers only when closed (not on pure timeout).
 */
static int cchan_rendezvous(cchan_t* c, void* data, int am_sender,
                            const struct timespec* deadline)
{
    for (;;) {
        if (c->closed) {
            if (!am_sender && data)
                memset(data, 0, c->msg_size);
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            return 0;
        }

        if (c->phase == 1 && c->want_send == (am_sender ? 1u : 0u)) {
            if (am_sender)
                memcpy(c->rendezvous, data, c->msg_size);
            else
                memcpy(data, c->rendezvous, c->msg_size);
            c->phase = 2;
            c->rendezvous = 0;
            CCHAN_OK(pthread_cond_broadcast(&c->can_send));
            CCHAN_OK(pthread_cond_broadcast(&c->can_recv));
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            cchan_obs_signal_all(c);
            return 1;
        }

        if (c->phase == 0) {
            c->phase = 1;
            c->want_send = am_sender ? 0u : 1u;
            c->rendezvous = data;
            /*
             * Signal observers with mu unlocked to avoid deadlock with
             * waiters that lock wait_mu then trylock c->mu.
             */
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            cchan_obs_signal_all(c);
            CCHAN_OK(pthread_mutex_lock(&c->mu));

            while (!c->closed && c->phase != 2) {
                int wr;
                if (deadline) {
                    if (am_sender)
                        wr = pthread_cond_timedwait(&c->can_send, &c->mu, deadline);
                    else
                        wr = pthread_cond_timedwait(&c->can_recv, &c->mu, deadline);
                    if (wr == ETIMEDOUT) {
                        /* Only abandon if still our wait slot and incomplete. */
                        if (c->phase == 1 && c->rendezvous == data) {
                            c->phase = 0;
                            c->rendezvous = 0;
                            CCHAN_OK(pthread_cond_broadcast(&c->can_send));
                            CCHAN_OK(pthread_cond_broadcast(&c->can_recv));
                            CCHAN_OK(pthread_mutex_unlock(&c->mu));
                            cchan_obs_signal_all(c);
                            return 0;
                        }
                        /* Peer completed or took the slot — recheck loop. */
                        if (c->phase == 2)
                            break;
                        continue;
                    }
                    if (wr != 0 && wr != EINTR)
                        CCHAN_ERR("timedwait rc=%d", wr);
                } else {
                    if (am_sender)
                        CCHAN_OK(pthread_cond_wait(&c->can_send, &c->mu));
                    else
                        CCHAN_OK(pthread_cond_wait(&c->can_recv, &c->mu));
                }
            }

            if (c->closed && c->phase != 2) {
                c->phase = 0;
                c->rendezvous = 0;
                if (!am_sender && data)
                    memset(data, 0, c->msg_size);
                CCHAN_OK(pthread_mutex_unlock(&c->mu));
                return 0;
            }

            /* Peer completed transfer (phase 2) or we completed as first side. */
            if (c->phase == 2) {
                c->phase = 0;
                c->rendezvous = 0;
                CCHAN_OK(pthread_cond_broadcast(&c->can_send));
                CCHAN_OK(pthread_cond_broadcast(&c->can_recv));
                CCHAN_OK(pthread_mutex_unlock(&c->mu));
                cchan_obs_signal_all(c);
                return 1;
            }

            /* Unexpected: re-loop */
            continue;
        }

        /* phase busy with another waiter of same role — wait for free slot */
        if (deadline) {
            int wr;
            if (am_sender)
                wr = pthread_cond_timedwait(&c->can_send, &c->mu, deadline);
            else
                wr = pthread_cond_timedwait(&c->can_recv, &c->mu, deadline);
            if (wr == ETIMEDOUT) {
                CCHAN_OK(pthread_mutex_unlock(&c->mu));
                return 0;
            }
        } else if (am_sender) {
            CCHAN_OK(pthread_cond_wait(&c->can_send, &c->mu));
        } else {
            CCHAN_OK(pthread_cond_wait(&c->can_recv, &c->mu));
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Send / receive                                                             */
/* -------------------------------------------------------------------------- */

CCHAN_API int cchan_send(cchan_t* c, const void* data)
{
    if (!c || !data) return 0;

    CCHAN_OK(pthread_mutex_lock(&c->mu));

    if (c->unbuffered)
        return cchan_rendezvous(c, (void*)data, 1, 0);

    while (!c->closed && cchan_full(c))
        CCHAN_OK(pthread_cond_wait(&c->can_send, &c->mu));

    if (c->closed) {
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return 0;
    }

    cchan_buf_write(c, data);
    CCHAN_OK(pthread_cond_signal(&c->can_recv));
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    cchan_obs_signal_all(c);
    return 1;
}

CCHAN_API int cchan_recv(cchan_t* c, void* data)
{
    if (!c || !data) return 0;

    CCHAN_OK(pthread_mutex_lock(&c->mu));

    if (c->unbuffered)
        return cchan_rendezvous(c, data, 0, 0);

    while (!c->closed && cchan_empty(c))
        CCHAN_OK(pthread_cond_wait(&c->can_recv, &c->mu));

    if (cchan_empty(c)) {
        memset(data, 0, c->msg_size);
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return 0;
    }

    cchan_buf_read(c, data);
    CCHAN_OK(pthread_cond_signal(&c->can_send));
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    cchan_obs_signal_all(c);
    return 1;
}

CCHAN_API int cchan_try_send(cchan_t* c, const void* data)
{
    if (!c || !data) return -1;

    CCHAN_OK(pthread_mutex_lock(&c->mu));

    if (c->closed) {
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return -1;
    }

    if (c->unbuffered) {
        if (c->phase == 1 && c->want_send == 1) {
            memcpy(c->rendezvous, data, c->msg_size);
            c->phase = 2;
            c->rendezvous = 0;
            CCHAN_OK(pthread_cond_broadcast(&c->can_send));
            CCHAN_OK(pthread_cond_broadcast(&c->can_recv));
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            cchan_obs_signal_all(c);
            return 1;
        }
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return 0;
    }

    if (cchan_full(c)) {
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return 0;
    }

    cchan_buf_write(c, data);
    CCHAN_OK(pthread_cond_signal(&c->can_recv));
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    cchan_obs_signal_all(c);
    return 1;
}

CCHAN_API int cchan_try_recv(cchan_t* c, void* data)
{
    if (!c || !data) return -1;

    CCHAN_OK(pthread_mutex_lock(&c->mu));

    if (c->unbuffered) {
        if (c->phase == 1 && c->want_send == 0) {
            memcpy(data, c->rendezvous, c->msg_size);
            c->phase = 2;
            c->rendezvous = 0;
            CCHAN_OK(pthread_cond_broadcast(&c->can_send));
            CCHAN_OK(pthread_cond_broadcast(&c->can_recv));
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            cchan_obs_signal_all(c);
            return 1;
        }
        if (c->closed) {
            memset(data, 0, c->msg_size);
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            return -1;
        }
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return 0;
    }

    if (cchan_empty(c)) {
        if (c->closed) {
            memset(data, 0, c->msg_size);
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            return -1;
        }
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return 0;
    }

    cchan_buf_read(c, data);
    CCHAN_OK(pthread_cond_signal(&c->can_send));
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    cchan_obs_signal_all(c);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Budgeted try + timeouts                                                    */
/* -------------------------------------------------------------------------- */

CCHAN_API int cchan_send_budget(cchan_t* c, const void* data, unsigned spins)
{
    unsigned i;
    int rc;
    if (!c || !data) return -1;
    for (i = 0; i <= spins; i++) {
        rc = cchan_try_send(c, data);
        if (rc != 0)
            return rc;
        if (i < spins)
            cchan_budget_yield(i);
    }
    return 0;
}

CCHAN_API int cchan_recv_budget(cchan_t* c, void* data, unsigned spins)
{
    unsigned i;
    int rc;
    if (!c || !data) return -1;
    for (i = 0; i <= spins; i++) {
        rc = cchan_try_recv(c, data);
        if (rc != 0)
            return rc;
        if (i < spins)
            cchan_budget_yield(i);
    }
    return 0;
}

CCHAN_API int cchan_send_timeout(cchan_t* c, const void* data, unsigned timeout_ms)
{
    struct timespec deadline;

    if (!c || !data) return -1;
    if (timeout_ms == 0)
        return cchan_try_send(c, data);

    cchan_deadline_ms(timeout_ms, &deadline);
    CCHAN_OK(pthread_mutex_lock(&c->mu));

    if (c->unbuffered) {
        int ok = cchan_rendezvous(c, (void*)data, 1, &deadline);
        /* rendezvous: 1 ok, 0 closed-or-timeout; map closed vs timeout */
        if (ok) return 1;
        /* Re-check closed without holding mu — safe snapshot */
        return cchan_is_closed(c) ? -1 : 0;
    }

    while (!c->closed && cchan_full(c)) {
        int wr = pthread_cond_timedwait(&c->can_send, &c->mu, &deadline);
        if (wr == ETIMEDOUT) {
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            return 0;
        }
        if (wr != 0 && wr != EINTR)
            CCHAN_ERR("send timedwait rc=%d", wr);
    }

    if (c->closed) {
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return -1;
    }

    cchan_buf_write(c, data);
    CCHAN_OK(pthread_cond_signal(&c->can_recv));
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    cchan_obs_signal_all(c);
    return 1;
}

CCHAN_API int cchan_recv_timeout(cchan_t* c, void* data, unsigned timeout_ms)
{
    struct timespec deadline;

    if (!c || !data) return -1;
    if (timeout_ms == 0)
        return cchan_try_recv(c, data);

    cchan_deadline_ms(timeout_ms, &deadline);
    CCHAN_OK(pthread_mutex_lock(&c->mu));

    if (c->unbuffered) {
        int ok = cchan_rendezvous(c, data, 0, &deadline);
        if (ok) return 1;
        if (cchan_is_closed(c)) {
            /* rendezvous already zeroed on close */
            return -1;
        }
        return 0; /* timeout */
    }

    while (!c->closed && cchan_empty(c)) {
        int wr = pthread_cond_timedwait(&c->can_recv, &c->mu, &deadline);
        if (wr == ETIMEDOUT) {
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            return 0;
        }
        if (wr != 0 && wr != EINTR)
            CCHAN_ERR("recv timedwait rc=%d", wr);
    }

    if (cchan_empty(c)) {
        memset(data, 0, c->msg_size);
        CCHAN_OK(pthread_mutex_unlock(&c->mu));
        return -1; /* closed and drained */
    }

    cchan_buf_read(c, data);
    CCHAN_OK(pthread_cond_signal(&c->can_send));
    CCHAN_OK(pthread_mutex_unlock(&c->mu));
    cchan_obs_signal_all(c);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Select                                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Snapshot readiness. Holds mu on every ready channel; caller must finish
 * via cchan_select_do or unlock.
 * Returns ready count, or -1 if all closed and nothing deliverable.
 */
static int cchan_select_poll(cchan_t** recv_chans, unsigned recv_count,
                             cchan_t** send_chans, unsigned send_count,
                             cchan_t** ready)
{
    unsigned i, total = recv_count + send_count;
    int n = 0;
    int open_or_busy = 0; /* open, or mutex busy (treat as still live) */

    for (i = 0; i < total; i++) {
        cchan_t* c = (i < recv_count) ? recv_chans[i] : send_chans[i - recv_count];
        int is_recv = (i < recv_count);
        int ok = 0;

        ready[i] = 0;
        if (!c) continue;

        /*
         * If the mutex is held, the channel is actively being used — do NOT
         * treat that as "closed". Returning -1 here was a real hang/EOF race
         * under load (select multi-recv with ASAN).
         */
        if (pthread_mutex_trylock(&c->mu) != 0) {
            open_or_busy = 1;
            continue;
        }

        if (!c->closed)
            open_or_busy = 1;

        if (c->unbuffered) {
            if (c->phase == 1) {
                if (is_recv && c->want_send == 0)
                    ok = 1;
                else if (!is_recv && c->want_send == 1)
                    ok = 1;
            } else if (is_recv && c->closed) {
                ok = 1; /* closed: select can complete with EOF */
            }
        } else {
            if (is_recv) {
                if (!cchan_empty(c))
                    ok = 1;
                else if (c->closed)
                    ok = 1;
            } else {
                if (!c->closed && !cchan_full(c))
                    ok = 1;
            }
        }

        if (ok) {
            ready[i] = c;
            n++;
        } else {
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
        }
    }

    if (n == 0 && !open_or_busy && total > 0)
        return -1;
    return n;
}

static int cchan_select_do(int n_ready,
                           void** recv_bufs, unsigned recv_count,
                           void** send_bufs, unsigned send_count,
                           cchan_t** ready)
{
    unsigned total = recv_count + send_count;
    unsigned i;
    int pick;
    int chosen = -1;
    cchan_t* c;

    if (n_ready <= 0)
        return -1;

    pick = (n_ready == 1) ? 0 : (rand() % n_ready);

    for (i = 0; i < total; i++) {
        if (!ready[i]) continue;
        if (pick == 0) {
            chosen = (int)i;
            break;
        }
        /*
         * Dropping an unchosen ready channel: other selectors may have failed
         * trylock while we held mu and parked on their wait_cv. Wake them so
         * they can claim this still-ready channel (critical for closed EOF).
         */
        {
            cchan_t* drop = ready[i];
            CCHAN_OK(pthread_mutex_unlock(&drop->mu));
            cchan_obs_signal_all(drop);
            ready[i] = 0;
        }
        pick--;
    }
    for (i = (unsigned)chosen + 1; i < total; i++) {
        if (ready[i]) {
            cchan_t* drop = ready[i];
            CCHAN_OK(pthread_mutex_unlock(&drop->mu));
            cchan_obs_signal_all(drop);
            ready[i] = 0;
        }
    }
    if (chosen < 0)
        return -1;

    c = ready[chosen];

    if ((unsigned)chosen < recv_count) {
        if (c->unbuffered) {
            if (c->phase == 1 && c->want_send == 0) {
                memcpy(recv_bufs[chosen], c->rendezvous, c->msg_size);
                c->phase = 2;
                c->rendezvous = 0;
                CCHAN_OK(pthread_cond_broadcast(&c->can_send));
                CCHAN_OK(pthread_cond_broadcast(&c->can_recv));
                CCHAN_OK(pthread_mutex_unlock(&c->mu));
                cchan_obs_signal_all(c);
            } else {
                /* closed empty — still wake other selectors parked after trylock miss */
                memset(recv_bufs[chosen], 0, c->msg_size);
                CCHAN_OK(pthread_mutex_unlock(&c->mu));
                cchan_obs_signal_all(c);
            }
        } else if (!cchan_empty(c)) {
            cchan_buf_read(c, recv_bufs[chosen]);
            CCHAN_OK(pthread_cond_signal(&c->can_send));
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            cchan_obs_signal_all(c);
        } else {
            /* closed empty buffered */
            memset(recv_bufs[chosen], 0, c->msg_size);
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            cchan_obs_signal_all(c);
        }
    } else {
        unsigned si = (unsigned)chosen - recv_count;
        if (c->unbuffered) {
            memcpy(c->rendezvous, send_bufs[si], c->msg_size);
            c->phase = 2;
            c->rendezvous = 0;
            CCHAN_OK(pthread_cond_broadcast(&c->can_send));
            CCHAN_OK(pthread_cond_broadcast(&c->can_recv));
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            cchan_obs_signal_all(c);
        } else {
            cchan_buf_write(c, send_bufs[si]);
            CCHAN_OK(pthread_cond_signal(&c->can_recv));
            CCHAN_OK(pthread_mutex_unlock(&c->mu));
            cchan_obs_signal_all(c);
        }
    }
    return chosen;
}

/*
 * Shared select wait path.
 * use_deadline == 0 → block forever (cchan_select)
 * use_deadline == 1 → timed wait; returns:
 *   >=0 case index, -1 timeout, -2 all-closed/empty/OOM
 * Blocking cchan_select maps -2 and -1 both as -1 (legacy).
 */
static int cchan_select_wait(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                             cchan_t** send_chans, void** send_bufs, unsigned send_count,
                             const struct timespec* deadline, int map_timeout_as_minus1)
{
    cchan_t** ready;
    pthread_mutex_t wait_mu;
    pthread_cond_t  wait_cv;
    unsigned total = recv_count + send_count;
    unsigned i;
    int n;
    int timed_out = 0;

    if (total == 0)
        return map_timeout_as_minus1 ? -1 : -2;

    ready = (cchan_t**)calloc(total, sizeof(cchan_t*));
    if (!ready)
        return map_timeout_as_minus1 ? -1 : -2;

    CCHAN_OK(pthread_mutex_init(&wait_mu, 0));
    CCHAN_OK(pthread_cond_init(&wait_cv, 0));

    /*
     * Register observers before the wait loop. Protocol for lost-wakeup safety:
     *   lock wait_mu
     *   poll; if ready unlock and proceed
     *   else cond_wait (atomically releases wait_mu)
     * Signaler locks wait_mu before signal, so a state change that occurs after
     * poll fails either delivers the signal while we are in cond_wait, or holds
     * wait_mu until we re-acquire and re-poll.
     */
    for (i = 0; i < total; i++) {
        cchan_t* c = (i < recv_count) ? recv_chans[i] : send_chans[i - recv_count];
        if (c) cchan_obs_add(c, &wait_mu, &wait_cv);
    }

    CCHAN_OK(pthread_mutex_lock(&wait_mu));
    for (;;) {
        n = cchan_select_poll(recv_chans, recv_count, send_chans, send_count, ready);
        if (n != 0)
            break;
        if (deadline) {
            int wr = pthread_cond_timedwait(&wait_cv, &wait_mu, deadline);
            if (wr == ETIMEDOUT) {
                /* Re-poll once: signal may have raced with timeout. */
                n = cchan_select_poll(recv_chans, recv_count, send_chans, send_count, ready);
                if (n != 0)
                    break;
                timed_out = 1;
                break;
            }
            if (wr != 0 && wr != EINTR)
                CCHAN_ERR("select timedwait rc=%d", wr);
        } else {
            CCHAN_OK(pthread_cond_wait(&wait_cv, &wait_mu));
        }
    }
    CCHAN_OK(pthread_mutex_unlock(&wait_mu));

    if (n > 0)
        n = cchan_select_do(n, recv_bufs, recv_count, send_bufs, send_count, ready);
    else if (timed_out)
        n = -1; /* timeout (select_timeout distinguishes from all-closed -2) */
    else if (n < 0 && !map_timeout_as_minus1)
        n = -2; /* all closed for select_timeout */
    /* else n already -1 for blocking cchan_select all-closed */

    for (i = 0; i < total; i++) {
        cchan_t* c = (i < recv_count) ? recv_chans[i] : send_chans[i - recv_count];
        if (c) cchan_obs_remove(c, &wait_cv);
    }

    CCHAN_OK(pthread_cond_destroy(&wait_cv));
    CCHAN_OK(pthread_mutex_destroy(&wait_mu));
    free(ready);
    return n;
}

CCHAN_API int cchan_select(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                           cchan_t** send_chans, void** send_bufs, unsigned send_count)
{
    return cchan_select_wait(recv_chans, recv_bufs, recv_count,
                             send_chans, send_bufs, send_count,
                             0, 1);
}

CCHAN_API int cchan_nb_select(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                              cchan_t** send_chans, void** send_bufs, unsigned send_count)
{
    cchan_t** ready;
    unsigned total = recv_count + send_count;
    int n;

    if (total == 0)
        return -1;

    ready = (cchan_t**)calloc(total, sizeof(cchan_t*));
    if (!ready)
        return -1;

    n = cchan_select_poll(recv_chans, recv_count, send_chans, send_count, ready);
    if (n > 0)
        n = cchan_select_do(n, recv_bufs, recv_count, send_bufs, send_count, ready);
    else if (n == 0)
        n = -1; /* none ready (channels still open) */

    free(ready);
    return n;
}

CCHAN_API int cchan_select_timeout(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                                   cchan_t** send_chans, void** send_bufs, unsigned send_count,
                                   unsigned timeout_ms)
{
    struct timespec deadline;
    int n;

    if (timeout_ms == 0) {
        n = cchan_nb_select(recv_chans, recv_bufs, recv_count,
                            send_chans, send_bufs, send_count);
        if (n >= 0)
            return n;
        /* nb_select maps both "none ready" and "all closed" to -1.
         * Distinguish via a direct poll so callers can stop cleanly. */
        {
            cchan_t** ready;
            unsigned total = recv_count + send_count;
            if (total == 0)
                return -2;
            ready = (cchan_t**)calloc(total, sizeof(cchan_t*));
            if (!ready)
                return -2;
            n = cchan_select_poll(recv_chans, recv_count, send_chans, send_count, ready);
            if (n > 0) {
                /* Raced: something became ready — complete it. */
                n = cchan_select_do(n, recv_bufs, recv_count, send_bufs, send_count, ready);
                free(ready);
                return n;
            }
            free(ready);
            return (n < 0) ? -2 : -1;
        }
    }

    cchan_deadline_ms(timeout_ms, &deadline);
    return cchan_select_wait(recv_chans, recv_bufs, recv_count,
                             send_chans, send_bufs, send_count,
                             &deadline, 0);
}

/* -------------------------------------------------------------------------- */
/* Typed helpers                                                              */
/* -------------------------------------------------------------------------- */

CCHAN_API int cchan_send_i32(cchan_t* c, int32_t v)
{
    if (!c || c->msg_size != sizeof(int32_t)) return 0;
    return cchan_send(c, &v);
}

CCHAN_API int cchan_recv_i32(cchan_t* c, int32_t* out)
{
    if (!c || !out || c->msg_size != sizeof(int32_t)) return 0;
    return cchan_recv(c, out);
}

CCHAN_API int cchan_send_i64(cchan_t* c, int64_t v)
{
    if (!c || c->msg_size != sizeof(int64_t)) return 0;
    return cchan_send(c, &v);
}

CCHAN_API int cchan_recv_i64(cchan_t* c, int64_t* out)
{
    if (!c || !out || c->msg_size != sizeof(int64_t)) return 0;
    return cchan_recv(c, out);
}

CCHAN_API int cchan_send_double(cchan_t* c, double v)
{
    if (!c || c->msg_size != sizeof(double)) return 0;
    return cchan_send(c, &v);
}

CCHAN_API int cchan_recv_double(cchan_t* c, double* out)
{
    if (!c || !out || c->msg_size != sizeof(double)) return 0;
    return cchan_recv(c, out);
}

CCHAN_API int cchan_send_buf(cchan_t* c, const void* buf, size_t len)
{
    if (!c || !buf || len != (size_t)c->msg_size) return 0;
    return cchan_send(c, buf);
}

CCHAN_API int cchan_recv_buf(cchan_t* c, void* buf, size_t len)
{
    if (!c || !buf || len != (size_t)c->msg_size) return 0;
    return cchan_recv(c, buf);
}

CCHAN_API void cchan_sleep(unsigned milliseconds)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(milliseconds / 1000u);
    ts.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

#ifdef __cplusplus
}
#endif

#endif /* CCHAN_IMPLEMENTATION */
#endif /* CCHAN_H */
