#include <stdio.h>
int main() {
    String s = "hello world";
    if (s.find("hello")) printf("BUGGY: says found-at-0 is falsy... wait\n");
    else printf("BUGGY IDIOM: match at index 0 is falsy, if-branch skipped\n");

    if (s.contains("hello")) printf("CORRECT: contains() says found\n");

    if (s.find("xyz") != (size_t)-1) printf("should not print\n");
    else printf("CORRECT: explicit != (size_t)-1 check says not found\n");
    return 0;
}
