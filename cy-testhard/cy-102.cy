/* Test 102: f-string with complex expressions and edge cases */
#include <stdio.h>

class Point {
    int x, y;
    Point(int x, int y) { this.x = x; this.y = y; }
    String toString() { return f"({this.x},{this.y})"; }
};

int add(int a, int b) { return a + b; }

int main() {
    int x = 10, y = 20;
    Point p(3, 4);

    // Basic f-string
    String s1 = f"x={x}, y={y}";
    printf("basic: %s\n", s1);

    // Expression in f-string
    String s2 = f"sum={add(x, y)}, product={x * y}";
    printf("expr: %s\n", s2);

    // Method call in f-string
    String s3 = f"point={p.toString()}";
    printf("method: %s\n", s3);

    // Nested f-string (string in string)
    String inner = "inner";
    String s4 = f"outer {f"nested {inner}"} end";
    printf("nested: %s\n", s4);

    // Dict in f-string
    dict d = { "key": "value", "num": 42 };
    String s5 = f"dict: {d.key} = {d.num}";
    printf("dict: %s\n", s5);

    // Escaped braces
    String s6 = f"literal {{braces}} and {x}";
    printf("escaped: %s\n", s6);

    return 0;
}
