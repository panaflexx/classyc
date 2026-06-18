/* Test 063: String with comparison */
#include <stdio.h>

int main() {
    String s1 = "hello";
    String s2 = "world";
    String s3 = "hello";
    
    if (s1 == s3) {
        printf("strings equal\n");
    }
    
    if (s1 != s2) {
        printf("strings not equal\n");
    }
    
    return 0;
}