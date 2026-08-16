#include "chan.h"

void worker(Chan<int> *ch, long base) {
    for (int i = 1; i <= 5; i++) ch->send((int)(base + i));
    ch->close();
}

int main(void) {
    auto ch = new Chan<int>(2);
    defer delete ch;
    go worker(ch, 100);
    int v = 0, sum = 0;
    while (ch->recv(&v)) sum += v;
    add_scheduler(1);
    printf("sum=%d (want 515)\n", sum);
    return sum == 515 ? 0 : 1;
}
