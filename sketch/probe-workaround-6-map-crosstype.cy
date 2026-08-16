#include <stdio.h>
#include "list.h"
int main() {
    int arr[] = {1, 2, 3, 4};
    List<String> *strs = arr.filter((int x) => x > 1)
                             .map((int x) => f"n{x}")
                             .ToList();
    for (auto s in strs) printf("%s\n", (char*)s);
    return 0;
}
