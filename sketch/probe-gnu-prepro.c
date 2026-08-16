/* Probe GNU/C23 preprocessor + attribute parsing used by Darwin/glibc headers. */
#pragma once

#define HAVE_X 1

#if __has_attribute(availability)
#error __has_attribute should be 0
#endif
#if __has_feature(nullability)
#error __has_feature should be 0
#endif

#ifdef HAVE_X
int a;
#elifdef HAVE_Y
int b;
#else
int c;
#endif

#ifndef MISSING
int d;
#elifndef HAVE_X
int e;
#else
int f;
#endif

__extension__ typedef int myint;

void *foo(int n) __attribute__((__malloc__)) __attribute__((__alloc_size__(1)));
int printf(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
void die(void) __attribute__((__noreturn__)) __asm__("die");
void *bar(void) __attribute__((__malloc__)) __attribute__((availability(macos, introduced = 10.10)));
void *pstar(void *__attribute__((unused)) q);

int main(void) {
  myint x = 1;
  return x + a + d + f;
}
