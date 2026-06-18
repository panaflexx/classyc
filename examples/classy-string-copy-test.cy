#include <stddef.h>

int printf(const char *, ...);
int strcmp(const char *, const char *);

static int fails = 0;

static void check(int cond, const char *label) {
  if (!cond) {
    printf("FAIL: %s\n", label);
    fails++;
  }
}

int main(void) {
  const char *buf = "hello, world from copy";
  String s = String.copy(buf, 5);
  check(strcmp(s, "hello") == 0, "copy-basic");
  check(s.length() == 5, "copy-len");

  String s2 = String.copy(buf + 7, 5); /* "world" */
  check(strcmp(s2, "world") == 0, "copy-offset");

  String empty = String.copy(buf, 0);
  check(empty.empty(), "copy-zero");

  if (fails == 0) {
    printf("ALL TESTS PASSED for String.copy\n");
  } else {
    printf("%d failures\n", fails);
  }
  return fails;
}
