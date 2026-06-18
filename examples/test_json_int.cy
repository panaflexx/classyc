/* Test that JSON-parsed integers are accessible via (int)(long)dict.field */
#include <stdio.h>

int main() {
    dict msg = json("{\"seq\":1,\"value\":42,\"neg\":-7,\"zero\":0,\"big\":999}");
    
    int seq = (int)(long)msg.seq;
    int val = (int)(long)msg.value;
    int neg = (int)(long)msg.neg;
    int z   = (int)(long)msg.zero;
    int big = (int)(long)msg.big;

    printf("seq=%d (expect 1)\n", seq);
    printf("value=%d (expect 42)\n", val);
    printf("neg=%d (expect -7)\n", neg);
    printf("zero=%d (expect 0)\n", z);
    printf("big=%d (expect 999)\n", big);

    int pass = (seq == 1 && val == 42 && neg == -7 && z == 0 && big == 999);
    printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
