#include "chan.h"

void sender(Chan<int> *ch) {
    int caught = 0;
    try {
        ch->send(1);
        ch->close();
        ch->send(2);              /* throws: send on closed channel */
    } catch (Exception e) {
        caught = 1;
        printf("fiber caught: %s\n", (char*) e.msg);
    }
    ch->close();                  /* would throw again (double close) — guard it */
    (void) caught;
}

int main(void) {
    auto ch = new Chan<int>(2);
    defer delete ch;
    int v = 0;
    go sender(ch);
    while (ch->recv(&v)) printf("got %d\n", v);
    add_scheduler(1);
    return 0;
}
