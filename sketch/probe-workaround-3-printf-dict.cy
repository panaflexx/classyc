#include <stdio.h>
int main() {
    dict cfg = { "host": "localhost", "timeout": 30 };
    printf("%s\n", (char*)json(cfg));   // stringify anything
    printf("%s\n", (char*)cfg.host);    // safe: host is a string leaf
    int t = (int)cfg.timeout;           // read numeric leaf as scalar
    printf("timeout=%d\n", t);
    return 0;
}
