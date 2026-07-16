/* val-047-uncaught-exit.cy — uncaught exceptions exit(1), not abort/core.
 *
 * Forks children that trip uncaught OOB / null-deref; expects exit status 1
 * (clean process failure). Parent always returns assertion failures count.
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-047-uncaught-exit.cy -eg
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "list.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* Returns child exit code, or 128+sig if signaled. */
int fork_fault(int mode) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        freopen("/dev/null", "w", stderr);
        freopen("/dev/null", "w", stdout);
        if (mode == 1) {
            auto xs = List<int>();
            xs.Add(1);
            (void) xs.Get(99); /* OutOfBoundsException, uncaught → exit(1) */
        } else if (mode == 2) {
            int *p = NULL;
            *p = 1; /* null-ptr safety trap → exit(1) */
        }
        _exit(0); /* not reached on fault */
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}

int main(void) {
    printf("=== val-047 uncaught exception → exit(1) ===\n\n");

    int c1 = fork_fault(1);
    check(c1 == 1, "1a List OOB uncaught exits 1");
    check(c1 != 134, "1b not SIGABRT (128+6)");

    int c2 = fork_fault(2);
    check(c2 == 1, "2a null-deref safety trap exits 1");
    check(c2 != 134, "2b not SIGABRT");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
