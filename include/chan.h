/* chan.h — Chan<T>: Go-style typed channels for ClassyC (needs -ffibers)
 *
 * A thin, monomorphized wrapper over the cyfiber runtime (include/cyfiber.h).
 * Each Chan<T> fixes the cchan message size to sizeof(T), so values are
 * memcpy'd through the channel exactly like Go value channels.
 *
 * Usage:
 *   #include "chan.h"
 *
 *   void producer(Chan<int> *ch) {
 *       for (int i = 1; i <= 10; i++) ch->send(i);   // parks if full
 *       ch->close();
 *   }
 *
 *   int main(void) {
 *       auto ch = new Chan<int>(4);          // buffered, capacity 4
 *       go producer(ch);                      // needs -ffibers
 *       int v, sum = 0;
 *       while (ch->recv(&v)) sum += v;        // false after close+drain
 *       add_scheduler(1);                     // explicit runtime
 *       delete ch;
 *       return sum == 55 ? 0 : 1;
 *   }
 *
 * Semantics (Go parity):
 *   new Chan<T>()      → unbuffered rendezvous channel (make(chan T))
 *   new Chan<T>(cap)   → buffered channel             (make(chan T, cap))
 *   ch->send(v)        → park until sent; THROWS RuntimeException if closed
 *   ch->recv(&v)       → park until a value arrives; returns bool ok
 *                        (false once closed AND drained — the `v, ok := <-ch`
 *                         idiom:  while (ch->recv(&v)) { ... })
 *   ch->close()        → close; THROWS on double close (Go panics)
 *   try_send/try_recv  → non-blocking: 1 ok, 0 would-block, -1 closed
 *   send/recv_timeout  → 1 ok, 0 timed out, -1 closed
 *   len/cap/closed     → cchan_size / cchan_capacity / cchan_is_closed
 *
 * Caveats:
 *   - Share channels by POINTER (Chan<T>*), like Go's reference semantics.
 *     The channel must outlive every fiber using it (stop → close → join
 *     before delete) — there is no GC.
 *   - Parking never blocks the OS worker thread (try + yield internally),
 *     so unbuffered channels between fibers on one worker are safe.
 *   - Sending a String copies the pointer: detach / heap-copy before handoff
 *     if the sending fiber may release its arena.
 *   - Throwing from send/close inside a fiber is supported in single-worker
 *     mode; the exception runtime is single-threaded (see cyfiber.h).
 */
#ifndef CLASSYC_CHAN_H
#define CLASSYC_CHAN_H

#include "cyfiber.h"

class Chan<T> {
    cy_chan raw;

    /* Unbuffered rendezvous channel (Go: make(chan T)). */
    Chan() {
        this->raw = cy_chan_create(0, (int) sizeof(T));
    }

    /* Buffered channel with the given capacity (Go: make(chan T, cap)). */
    Chan(int cap) {
        this->raw = cy_chan_create(cap, (int) sizeof(T));
    }

    ~Chan() {
        if (this->raw) cy_chan_dispose(this->raw);
    }

    /* Park until the value is sent.  Throws RuntimeException on send to a
       closed channel (Go panics). */
    void send(T v) {
        if (!cy_chan_send_park(this->raw, &v))
            throw(RuntimeException, "send on closed channel");
    }

    /* Park until a value arrives; false once closed AND drained. */
    bool recv(T *out) {
        return cy_chan_recv_park(this->raw, out) != 0;
    }

    /* Non-blocking: 1 transferred, 0 would-block, -1 closed. */
    int try_send(T v)    { return cy_chan_try_send(this->raw, &v); }
    int try_recv(T *out) { return cy_chan_try_recv(this->raw, out); }

    /* Budgeted: 1 transferred, 0 timed out, -1 closed. */
    int send_timeout(T v, int ms)    { return cy_chan_send_timeout(this->raw, &v, ms); }
    int recv_timeout(T *out, int ms) { return cy_chan_recv_timeout(this->raw, out, ms); }

    /* Close the channel; receivers drain then see false.  Throws on
       double close (Go panics). */
    void close() {
        if (!cy_chan_close(this->raw))
            throw(RuntimeException, "close of closed channel");
    }

    bool closed() { return cy_chan_is_closed(this->raw) != 0; }
    int  len()    { return cy_chan_size(this->raw); }
    int  cap()    { return cy_chan_capacity(this->raw); }
};

#endif /* CLASSYC_CHAN_H */
