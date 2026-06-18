/* Test 031: Lambda with array sorting */
#include <stdio.h>

void sort(int *arr, int n, int (*cmp)(int, int)) {
    for (int i = 0; i < n; i++) {
        for (int j = i+1; j < n; j++) {
            if (cmp(arr[i], arr[j]) > 0) {
                int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
            }
        }
    }
}

int main() {
    int arr[] = {5, 2, 8, 1, 9};
    sort(arr, 5, (int a, int b) => a - b);
    printf("sorted: %d, %d, %d, %d, %d\n", arr[0], arr[1], arr[2], arr[3], arr[4]);
    return 0;
}