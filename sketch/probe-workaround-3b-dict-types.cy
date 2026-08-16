#include <stdio.h>
#include "dict_types.h"
int main() {
    dict cfg = { "host": "localhost", "timeout": 30 };
    dict h = cfg.host;
    dict t = cfg.timeout;
    switch (h.type()) {
        case DICT_STRING: printf("host is a string: %s\n", (char*)h); break;
        default: printf("host: unexpected type\n"); break;
    }
    switch (t.type()) {
        case DICT_INT64:  printf("timeout is int64: %d\n", (int)t); break;
        case DICT_NUMBER: printf("timeout is number: %f\n", (double)t); break;
        default: printf("timeout: unexpected type\n"); break;
    }
    return 0;
}
