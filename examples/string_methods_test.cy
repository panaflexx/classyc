#include <stddef.h>
int printf(const char *, ...);

int main() {
    String mystring = "Hello, this is a test";

    String substr = mystring.substr(0, 5);
    printf("substr(0,5)  = [%s]\n", substr);          /* Hello */

    size_t slen = mystring.length();
    printf("length()     = %lu\n", slen);              /* 21 */

    size_t pos = mystring.find("this");
    printf("find(\"this\") = %lu\n", pos);              /* 9 */

    mystring.replace(pos, 4, "that");
    printf("after replace= [%s]\n", mystring);         /* Hello, that is a test */

    String x;
    if (x == null || x.empty()) {
        printf("x is null or empty\n");
    }

    String y = "";
    if (y == null || y.empty()) {
        printf("y is null or empty\n");
    }

    /* UTF-8: 'Grüße' has 5 code points but more bytes */
    String u = "Grüße";
    printf("utf8 length  = %lu\n", u.length());        /* 5 */
    printf("utf8 substr  = [%s]\n", u.substr(0, 3));    /* Grü */

    if (mystring != null) printf("mystring is not null\n");

    return 0;
}
