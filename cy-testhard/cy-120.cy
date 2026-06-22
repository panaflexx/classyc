/* Test 120: Complex expression statements and statement expressions */
#include <stdio.h>

int main() {
    // Statement expression (GNU extension)
    int x = ({ int a = 10; int b = 20; a + b; });
    printf("stmt expr: %d\n", x);

    // Nested statement expressions
    int y = ({
        int z = ({ int a = 5; a * 2; });
        z + 10;
    });
    printf("nested stmt expr: %d\n", y);

    // Label as value (GNU extension)
    void* labels[] = { &&label1, &&label2, &&label3 };
    int idx = 1;
    goto *labels[idx];

label1:
    printf("label1\n");
    goto end;
label2:
    printf("label2\n");
    goto end;
label3:
    printf("label3\n");
    goto end;

end:
    // Compound literal
    int* arr = (int[]){1, 2, 3, 4, 5};
    printf("compound literal: ");
    for (int i = 0; i < 5; i++) printf("%d ", arr[i]);
    printf("\n");

    // Compound literal with designated initializers
    struct { int x; int y; }* pts = (struct { int x; int y; }[]){
        { .x = 1, .y = 2 },
        { .x = 3, .y = 4 }
    };
    printf("compound struct: (%d,%d) (%d,%d)\n", pts[0].x, pts[0].y, pts[1].x, pts[1].y);

    return 0;
}
