/* Test 112: Complex initialization with designated initializers */
#include <stdio.h>

class Point {
    int x, y, z;
};

class Rect {
    Point topLeft;
    Point bottomRight;
    String label;
};

int main() {
    // Nested designated initializers
    Rect r = {
        .topLeft = { .x = 0, .y = 0, .z = 0 },
        .bottomRight = { .x = 10, .y = 20, .z = 0 },
        .label = "rectangle"
    };
    printf("rect: (%d,%d) - (%d,%d) label=%s\n",
           r.topLeft.x, r.topLeft.y, r.bottomRight.x, r.bottomRight.y, r.label);

    // Array with designated initializers
    int arr[10] = { [0] = 1, [5] = 10, [9] = 100 };
    printf("array: ");
    for (int i = 0; i < 10; i++) printf("%d ", arr[i]);
    printf("\n");

    // Dict with complex nested designated init
    dict complex = {
        .points = {
            { .x = 1, .y = 2 },
            { .x = 3, .y = 4 }
        },
        .meta = { .created = "now", .version = 1 }
    };
    printf("complex: p0=(%d,%d) v=%d\n",
           complex.points[0].x, complex.points[0].y, complex.meta.version);

    return 0;
}
