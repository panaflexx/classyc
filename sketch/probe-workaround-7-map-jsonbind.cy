#include <stdio.h>
#include "map.h"
int main() {
    dict d = json("{\"scores\":{\"alice\":90,\"bob\":75}}");
    dict scores = d.scores;

    auto m = Map<String, int>();
    for (auto key, val in scores)
        m[(String)key] = (int)val;

    printf("alice=%d bob=%d count=%d\n", m["alice"], m["bob"], m.Count());
    return 0;
}
