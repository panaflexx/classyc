#include <stdio.h>
#include "dict.h"
int main() {
    dict cfg = { "host": "localhost", "timeout": 30 };
    dict h = cfg.host;
    /* Tempting but wrong: cast to the runtime's DictValue* and read ->type
       directly, thinking it's the tag. */
    DictValue *raw = (DictValue*)h;
    printf("raw->type as if it were the tag: %d (DICT_STRING is %d)\n", raw->type, DICT_STRING);
    return 0;
}
