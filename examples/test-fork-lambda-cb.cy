/* test-fork-lambda-cb.c — minimal test of the exact fork+pipe+lambda-callback
 * pattern used by jitrunner for DAP output capture.
 *
 * Goal: verify that a lambda passed as OutputCB (function pointer) can be
 * invoked from the *parent* after the fork point, using a user-data pointer
 * that was captured before the fork (passed explicitly via the void*).
 *
 * Language facts (C11 + ClassyC extensions):
 *   - Lambdas are lowered to *static* named functions.  No closures.
 *     A lambda expression like `(void *u, char *c, char *t) => { ... }`
 *     produces a plain function pointer; the pointer value is valid in
 *     both parent and child after fork (code is shared).
 *   - The lambda body in this pattern *never* refers to enclosing locals
 *     by name; the only "environment" is the explicit `void *user` argument.
 *   - Classes, methods, `new`, `defer`, f-strings etc. are all supported.
 *
 * Run (from project root):
 *     ./bin/classyc examples/test-fork-lambda-cb.c -eg
 *
 * Expected: child prints (redirected), parent receives data via the lambda
 * calls, prints [cb] lines, and reports PASS with count > 0.
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>   /* F_GETFL, F_SETFL, O_NONBLOCK */

extern int    fork(void);
extern void   _exit(int status);
extern int    close(int fd);
extern long   read(int fd, void *buf, long count);
extern unsigned usleep(unsigned usec);
extern int    waitpid(int pid, int *status, int options);
extern int    dup(int fd);
extern int    dup2(int oldfd, int newfd);
extern int    pipe(int pipefd[2]);
extern int    fcntl(int fd, int cmd, ...);

#ifndef WNOHANG
#define WNOHANG 1
#endif

extern int *__errno_location(void);

typedef void (*OutputCB)(void *user, char *category, char *text);

/* A simple receiver class that stands in for DapServer in the real code.
 * The lambda will cast the void* back to this and call a method. */
class Handler {
    int calls;

    Handler() {
        this.calls = 0;
    }

    void receive(char *cat, char *text) {
        this.calls = this.calls + 1;
        /* Print inside the callback so we see it was reached from the read loop
           in the parent after fork.  Keep it minimal. */
        printf("  [cb] cat=%s len=%d first=%02x text=%s",
               cat, (int)strlen(text), (unsigned char)text[0], text);
    }

    int count() { return this.calls; }
};

/* Stripped-down equivalent of run_bmir's capture path (no JIT, no .bmir).
 * Forks, child writes to stdout (simulating the executed program),
 * parent reads the pipe non-blockingly and delivers every chunk via the
 * supplied callback + user pointer.  After child exits, drains any
 * remaining data and returns the exit code. */
int run_with_cb(OutputCB cb, void *user) {
    int out_pipe[2] = {-1, -1};

    if (pipe(out_pipe) < 0) {
        printf("[test] pipe() failed\n");
        return 127;
    }

    int pid = fork();
    if (pid < 0) {
        printf("[test] fork() failed\n");
        close(out_pipe[0]);
        close(out_pipe[1]);
        return 127;
    }

    if (pid == 0) {
        /* child: redirect 1 to the write end of the pipe, write some output
           that matches what a real program (the .bmir) would emit. */
        close(out_pipe[0]);
        dup2(out_pipe[1], 1);
        close(out_pipe[1]);

        printf("Hello, this is a test\n");
        printf("go! %p\n", (void*)0x1234);   /* simulate address print */
        printf("snatch find = [-10] hello = \"Snarf snarf!\"\n");
        printf("Hello !!! Schöne Grüße 😊 !!!cats\n");  /* unicode like classy.c */

        fflush(stdout);
        fflush(stderr);
        _exit(17);   /* non-zero exit to exercise the return path */
    }

    /* parent */
    close(out_pipe[1]);

    /* make the read end non-blocking, exactly like the real code */
    int fl = fcntl(out_pipe[0], F_GETFL, 0);
    fcntl(out_pipe[0], F_SETFL, fl | O_NONBLOCK);

    char buf[4096];
    int out_open = 1;

    while (1) {
        if (out_open) {
            long n = read(out_pipe[0], buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                cb(user, "stdout", buf);
            } else if (n == 0) {
                out_open = 0;
                close(out_pipe[0]);
            } else {
                if (*__errno_location() != 11 /* EAGAIN */) {
                    out_open = 0;
                    close(out_pipe[0]);
                }
            }
        }

        int wstatus = 0;
        int wr = waitpid(pid, &wstatus, WNOHANG);
        if (wr == pid) {
            /* child exited — drain any remainder (switch back to blocking) */
            if (out_open) {
                fcntl(out_pipe[0], F_SETFL, 0);
                long n;
                while ((n = read(out_pipe[0], buf, sizeof(buf)-1)) > 0) {
                    buf[n] = '\0';
                    cb(user, "stdout", buf);
                }
                close(out_pipe[0]);
            }
            if ((wstatus & 0x7f) == 0) {
                return (wstatus >> 8) & 0xff;
            } else {
                return 128 + (wstatus & 0x7f);
            }
        }

        usleep(2000);
    }

    /* unreachable */
    return 255;
}

int main() {
    printf("=== fork + lambda callback test (DAP-style output capture) ===\n");

    Handler *h = new Handler();

    /* THE CRITICAL CALL — identical shape to the jitrunner site:
     *   run_xxx( path, mode, verbose,
     *            (void *u, char *cat, char *txt) => { ((DapServer*)u)->send_output(cat, txt); },
     *            (void *)dap );
     *
     * The lambda here is a plain function pointer (no capture).  We pass the
     * Handler instance explicitly as the user pointer, exactly as the real
     * DapServer* is passed.  The lambda body only uses the passed-in u.
     */
    int exit_code = run_with_cb(
        (void *u, char *cat, char *txt) => { ((Handler*)u)->receive(cat, txt); },
        (void *)h
    );

    printf("\nrun_with_cb returned %d\n", exit_code);
    printf("handler saw %d callback(s)\n", h->count());

    int result;
    if (h->count() > 0) {
        printf("\nPASS: lambda callback was invoked from parent after fork\n");
        printf("      (the design pattern itself works)\n");
        result = 0;
    } else {
        printf("\nFAIL: zero callbacks received — lambda was never called\n");
        result = 1;
    }
    return result;
}
