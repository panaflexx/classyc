/* jitrunner.c — ClassyC hot-reload runner for .bmir files
 *
 * A dogfood program written in ClassyC that:
 *   1. Loads one or more .bmir files into one MIR context and JIT-runs main()
 *      in a forked child (runtime link: HTTP package + app, etc.)
 *   2. Watches those files for changes (inotify on Linux)
 *   3. On change, kills any running child and re-runs (hot-reload)
 *   4. Optionally acts as a DAP server for IDE debugging (--dap <port>)
 *
 * Build (from project root):
 *   bash src/jitrunner/build.sh
 *
 * Usage:
 *   bin/jitrunner path/to/program.bmir [--watch] [--mode lazy|gen|interp]
 *   bin/jitrunner lib.bmir app.bmir --mode gen
 *   bin/jitrunner --compile src.c -o program.bmir [--watch]
 *   bin/jitrunner --dap 4711                  # DAP server mode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/file.h"
#include "include/term.h"
#include <stdlib.h>
#include <fcntl.h>  /* for F_GETFL, F_SETFL, O_NONBLOCK */

/* Declare POSIX functions we need without pulling in <unistd.h>
   (which conflicts with File::write). */
extern int    fork(void);
extern void   _exit(int status);
extern int    close(int fd);
extern long   read(int fd, void *buf, long count);
extern unsigned usleep(unsigned usec);
extern int    waitpid(int pid, int *status, int options);
extern int    kill(int pid, int sig);
extern int    dup(int fd);
extern int    dup2(int oldfd, int newfd);

/* For output capture in run_bmir (DAP mode) */
extern int    pipe(int pipefd[2]);
extern int    fcntl(int fd, int cmd, ...);

/* For DAP debug logging to dapdebug.log */
/*
#define O_WRONLY   1
#define O_CREAT    64
#define O_APPEND   1024
extern int open(const char *pathname, int flags, unsigned mode);
*/

/* waitpid option (WNOHANG not pulled in without sys/wait.h) */
#ifndef WNOHANG
#define WNOHANG   1
#endif
#ifndef SIGTERM
#define SIGTERM   15
#endif
#ifndef SIGKILL
#define SIGKILL   9
#endif
#define JIT_MAX_BMIR 32

/* inotify */
extern int    inotify_init(void);
extern int    inotify_add_watch(int fd, char *path, unsigned mask);

/* time — struct timespec is already visible from libc headers */
extern int    clock_gettime(int clk_id, void *tp);

/* Output callback for capturing program stdout/stderr (used in DAP mode) */
typedef void (*OutputCB)(void *user, char *category, char *text);

/* errno */
extern int   *__errno_location(void);

/* system() */
extern int    system(char *cmd);

/* ═══════════════════════════════════════════════════════════════════════
   MIR bridge — extern declarations for the C bridge functions
   ═══════════════════════════════════════════════════════════════════════ */

typedef void *JIT_context;
typedef void *JIT_module;
typedef void *JIT_item;

extern JIT_context jit_init(void);
extern void        jit_finish(JIT_context ctx);
extern int         jit_read_bmir(JIT_context ctx, char *path);
extern JIT_module  jit_first_module(JIT_context ctx);
extern JIT_module  jit_next_module(JIT_module m);
extern JIT_item    jit_first_item(JIT_module m);
extern JIT_item    jit_next_item(JIT_item item);
extern int         jit_item_is_func(JIT_item item);
extern char       *jit_func_name(JIT_item item);
extern void        jit_load_module(JIT_context ctx, JIT_module m);
extern void        jit_gen_init(JIT_context ctx);
extern void        jit_gen_finish(JIT_context ctx);
extern void        jit_link(JIT_context ctx, int mode);
extern void       *jit_get_func_addr(JIT_item item);
extern void       *jit_gen_func(JIT_context ctx, JIT_item item);

/* Cooperative interp debug control-pipe endpoints (defined in mir-bridge.c) */
extern int g_interp_child_ctrl_fd_from_env;
extern int g_dap_ctrl_write_fd;

/* ═══════════════════════════════════════════════════════════════════════
   RunConfig — parsed command-line options
   ═══════════════════════════════════════════════════════════════════════ */

class RunConfig {
    String bmir_path;       /* last / primary .bmir (DAP, -o, display) */
    char **bmir_paths;      /* all .bmir files, load order; last main() wins */
    int    nbmirs;
    String source_path;     /* optional: .c source to compile first   */
    String compiler_path;   /* path to classyc compiler               */
    int    watch;           /* 1 = watch for changes and re-run       */
    int    jit_mode;        /* 0=lazy  1=gen  2=interp                */
    int    verbose;         /* 1 = extra diagnostic output            */
    int    dap_port;        /* >0 = run as DAP server on this port    */
    int    dap_stdio;       /* 1 = DAP over stdin/stdout (for Zed)    */

    RunConfig() {
        this.bmir_path     = "";
        this.bmir_paths    = (char **) malloc(JIT_MAX_BMIR * 8);
        this.nbmirs        = 0;
        this.source_path   = "";
        this.compiler_path = "./bin/classyc";
        this.watch         = 0;
        this.jit_mode      = 0;
        this.verbose       = 0;
        this.dap_port      = 0;
        this.dap_stdio     = 0;
        if (this.bmir_paths) {
            for (int i = 0; i < JIT_MAX_BMIR; i++)
                this.bmir_paths[i] = 0;
        }
    }

    ~RunConfig() {
        if (this.bmir_paths) free((void *) this.bmir_paths);
        this.bmir_paths = 0;
    }

    void add_bmir(char *path) {
        if (!path || !path[0] || !this.bmir_paths) return;
        for (int i = 0; i < this.nbmirs; i++) {
            if (this.bmir_paths[i] && strcmp(this.bmir_paths[i], path) == 0)
                return;
        }
        if (this.nbmirs >= JIT_MAX_BMIR) return;
        this.bmir_paths[this.nbmirs] = path;
        this.nbmirs = this.nbmirs + 1;
        this.bmir_path = path;
    }
};

/* Global DAP debug log fd, shared with dap.h (declared extern earlier) */
int dap_logger_fd = -1;

/* ═══════════════════════════════════════════════════════════════════════
   RunResult — outcome of a single JIT execution
   ═══════════════════════════════════════════════════════════════════════ */

class RunResult {
    int  exit_code;
    int  signal_num;       /* non-zero if child was killed by signal  */
    int  timed_out;
    int  reloaded;         /* 1 = parent SIGTERM'd child because a .bmir changed */
    long elapsed_ms;       /* wall-clock milliseconds                 */

    RunResult() {
        this.exit_code  = -1;
        this.signal_num = 0;
        this.timed_out  = 0;
        this.reloaded   = 0;
        this.elapsed_ms = 0;
    }

    ~RunResult() {}

    int ok() {
        return this.exit_code == 0 && this.signal_num == 0;
    }

    String summary() {
        if (this.signal_num != 0)
            return f"killed by signal {this.signal_num} ({this.elapsed_ms}ms)";
        if (this.timed_out)
            return f"timed out ({this.elapsed_ms}ms)";
        return f"exit {this.exit_code} ({this.elapsed_ms}ms)";
    }
};

/* ═══════════════════════════════════════════════════════════════════════
   Time helpers
   ═══════════════════════════════════════════════════════════════════════ */

/* Pack RunConfig's .bmir list plus an optional extra path (DAP launch). */
static int collect_bmirs(RunConfig *cfg, char *extra, char **out, int cap) {
    int n = 0;
    if (cfg && cfg->bmir_paths) {
        for (int i = 0; i < cfg->nbmirs && n < cap; i++) {
            if (cfg->bmir_paths[i]) {
                out[n] = cfg->bmir_paths[i];
                n = n + 1;
            }
        }
    }
    if (extra && extra[0]) {
        int dup = 0;
        for (int i = 0; i < n; i++) {
            if (out[i] && strcmp(out[i], extra) == 0) dup = 1;
        }
        if (!dup && n < cap) {
            out[n] = extra;
            n = n + 1;
        }
    }
    return n;
}

long time_ms(void) {
    long buf[2];  /* tv_sec, tv_nsec */
    buf[0] = 0;
    buf[1] = 0;
    clock_gettime(0 /* CLOCK_REALTIME */, (void *)buf);
    return buf[0] * 1000 + buf[1] / 1000000;
}

/* ═══════════════════════════════════════════════════════════════════════
   inotify — watch one or more paths (non-blocking poll for hot-reload)
   ═══════════════════════════════════════════════════════════════════════ */

static int watch_open(char **paths, int n) {
    int ifd = inotify_init();
    if (ifd < 0) return -1;
    int fl = fcntl(ifd, F_GETFL, 0);
    if (fl >= 0) fcntl(ifd, F_SETFL, fl | O_NONBLOCK);
    int added = 0;
    for (int i = 0; i < n; i++) {
        char *path = paths[i];
        if (!path || !path[0]) continue;
        int wd = inotify_add_watch(ifd, path, 0x002 | 0x008 | 0x080);
        if (wd < 0) {
            char *dir = (char *) malloc(strlen(path) + 1);
            strcpy(dir, path);
            char *slash = strrchr(dir, '/');
            if (slash) *slash = '\0';
            else strcpy(dir, ".");
            wd = inotify_add_watch(ifd, dir, 0x100 | 0x080);
            free(dir);
        }
        if (wd >= 0) added = added + 1;
    }
    if (!added) {
        close(ifd);
        return -1;
    }
    return ifd;
}

static int watch_poll(int ifd) {
    if (ifd < 0) return 0;
    char buf[4096];
    long n = read(ifd, (void *) buf, 4096);
    if (n > 0) return 1;
    return 0;
}

static void kill_child(int pid) {
    kill(pid, SIGTERM);
    for (int i = 0; i < 50; i++) {
        int st = 0;
        int wr = waitpid(pid, &st, WNOHANG);
        if (wr == pid) return;
        usleep(20000);
    }
    kill(pid, SIGKILL);
    int st = 0;
    waitpid(pid, &st, 0);
}

/* ═══════════════════════════════════════════════════════════════════════
   JIT execution — fork + JIT in child
   ═══════════════════════════════════════════════════════════════════════ */

RunResult *run_bmir(char **paths, int npaths, int jit_mode, int verbose,
                    OutputCB out_cb, void *cb_user,
                    char **watch_paths, int nwatch) {
    RunResult *result = new RunResult();
    long start = time_ms();

    if (verbose) {
        printf("[jitrunner] loading %d .bmir (mode=%d)\n", npaths, jit_mode);
        for (int i = 0; i < npaths; i++)
            printf("[jitrunner]   %s\n", paths[i]);
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};

    if (out_cb) {
        if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
            printf("[jitrunner] error: pipe() failed\n");
            result->exit_code = 127;
            result->elapsed_ms = time_ms() - start;
            return result;
        }
    }

    fflush(stdout);
    fflush(stderr);

    int pid = fork();

    if (pid < 0) {
        printf("[jitrunner] error: fork() failed\n");
        if (out_cb) { close(out_pipe[0]); close(out_pipe[1]); close(err_pipe[0]); close(err_pipe[1]); }
        result->exit_code = 127;
        result->elapsed_ms = time_ms() - start;
        return result;
    }

    if (pid == 0) {
        /* child - log env for debugging */
        {
            char *e = getenv("CLASSYC_DEBUG_BPS");
            char *e2 = getenv("CLASSYC_DEBUG_CTRL_FD");
            fprintf(stderr, "CHILD ENV BPS=%s CTRL=%s mode=%d\n", e?e:"NULL", e2?e2:"NULL", jit_mode); fflush(stderr);
            if (dap_logger_fd>=0) dprintf(dap_logger_fd, "CHILD ENV BPS=%s CTRL=%s mode=%d\n", e?e:"NULL", e2?e2:"NULL", jit_mode);
        }
        /* Child only needs the control-pipe READ end.  Closing the write
           end ensures parent-death → EOF on the child's read. */
        if (g_dap_ctrl_write_fd >= 0) {
            close(g_dap_ctrl_write_fd);
            g_dap_ctrl_write_fd = -1;
        }
        /* ── child process ──────────────── */
        if (out_cb) {
            /* Redirect stdout(1)/stderr(2) to the pipe write ends so the parent
               captures the JIT'd program's output, then close all the pipe fds
               we no longer need in the child. */
            close(out_pipe[0]);
            close(err_pipe[0]);
            dup2(out_pipe[1], 1);
            dup2(err_pipe[1], 2);
            close(out_pipe[1]);
            close(err_pipe[1]);
        }

        JIT_context ctx = jit_init();
        if (!ctx) {
            fprintf(stderr, "[jitrunner] error: jit_init() failed\n");
            _exit(126);
        }

        if (npaths < 1 || !paths) {
            fprintf(stderr, "[jitrunner] error: no .bmir files to load\n");
            jit_finish(ctx);
            _exit(125);
        }
        for (int i = 0; i < npaths; i++) {
            if (jit_read_bmir(ctx, paths[i]) != 0) {
                fprintf(stderr, "[jitrunner] error: cannot read %s\n", paths[i]);
                jit_finish(ctx);
                _exit(125);
            }
        }

        JIT_item main_func = 0;
        int nmain = 0;
        JIT_module mod = jit_first_module(ctx);
        while (mod) {
            JIT_item item = jit_first_item(mod);
            while (item) {
                if (jit_item_is_func(item)) {
                    char *name = jit_func_name(item);
                    if (name && strcmp(name, "main") == 0) {
                        main_func = item; /* last definition wins */
                        nmain = nmain + 1;
                    }
                }
                item = jit_next_item(item);
            }
            jit_load_module(ctx, mod);
            mod = jit_next_module(mod);
        }

        if (nmain > 1)
            fprintf(stderr, "[jitrunner] warning: %d main() symbols; using the last\n", nmain);

        if (!main_func) {
            fprintf(stderr, "[jitrunner] error: no main() found in:");
            for (int i = 0; i < npaths; i++)
                fprintf(stderr, " %s", paths[i]);
            fprintf(stderr, "\n");
            jit_finish(ctx);
            _exit(124);
        }

        jit_gen_init(ctx);
        if(jit_mode==2){
            char *bp=getenv("CLASSYC_DEBUG_BPS");
            if(bp&&bp[0]){
                extern void *jit_interp_dbg_new(void);
                extern int jit_interp_dbg_add_bp_resolved(void *st, void *ctx, char *file, int line);
                extern void jit_interp_dbg_set_state(void *st);
                extern void jit_interp_dbg_set_break_cb(void *st, void *cb, void *user);
                extern void interp_child_on_break_simple(void *u, void *f, int fid, int l, int c);
                int fd=open(bp,0,0);
                if(fd>=0){
                    void *dbg=jit_interp_dbg_new();
                    char bbuf[8192];
                    long rn=read(fd,bbuf,sizeof(bbuf)-1);
                    if (dap_logger_fd>=0) dprintf(dap_logger_fd,"CHILD loading bps file %s fd=%d\n", bp, fd);
                    close(fd);
                    if(rn>0){
                        bbuf[rn]='\0';
                        char *q=bbuf;
                        while(*q){
                            char *nl=strchr(q,'\n');
                            if(nl) *nl='\0';
                            char *cc=strrchr(q,':');
                            if(cc){
                                *cc='\0';
                                int ll=atoi(cc+1);
                                if(ll>0){
                                    if(dap_logger_fd>=0) dprintf(dap_logger_fd,"CHILD add bp %s:%d (resolving)\n", q, ll);
                                    /* Map to nearest line that actually has MIR loc —
                                       blank lines / expressions w/o own locs still break. */
                                    jit_interp_dbg_add_bp_resolved(dbg, ctx, q, ll);
                                }
                            }
                            if(!nl) break;
                            q=nl+1;
                        }
                    }
                    jit_interp_dbg_set_break_cb(dbg,(void*)interp_child_on_break_simple,(void*)ctx);
                    jit_interp_dbg_set_state(dbg);
                }
            }
        }
        jit_link(ctx, jit_mode);

        void *addr = jit_get_func_addr(main_func);
        if (!addr) {
            fprintf(stderr, "[jitrunner] error: main() has no address\n");
            jit_gen_finish(ctx);
            jit_finish(ctx);
            _exit(123);
        }

        int (*entry)(int, char**, char**) = addr;
        int code = entry(0, (char **)0, (char **)0);

        fflush(stdout);
        fflush(stderr);

        jit_gen_finish(ctx);
        jit_finish(ctx);
        _exit(code);
    }

    /* ── parent process ───────────────────────── */
    int wfd = -1;
    if (nwatch > 0 && watch_paths)
        wfd = watch_open(watch_paths, nwatch);

    int out_r = out_pipe[0];
    int err_r = err_pipe[0];

    /* Parent only needs the control-pipe WRITE end. */
    if (g_interp_child_ctrl_fd_from_env >= 0) {
        close(g_interp_child_ctrl_fd_from_env);
        g_interp_child_ctrl_fd_from_env = -1;
    }

    if (out_cb) {
        /* Close the write ends in the parent so the read ends see EOF once the
           child exits (and closes its copies). */
        close(out_pipe[1]);
        close(err_pipe[1]);
        /* make read ends non-blocking */
        int fl = fcntl(out_r, F_GETFL, 0);
        fcntl(out_r, F_SETFL, fl | O_NONBLOCK);
        fl = fcntl(err_r, F_GETFL, 0);
        fcntl(err_r, F_SETFL, fl | O_NONBLOCK);
    }

    if (out_cb) {
        char buf[4096];
        int out_open = 1, err_open = 1;

        while (1) {
            if (out_open) {
                long n = read(out_r, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    out_cb(cb_user, "stdout", buf);
                } else if (n == 0) {
                    out_open = 0;
                    close(out_r);
                } else {
                    if (*__errno_location() != 11 /* EAGAIN */) {
                        out_open = 0;
                        close(out_r);
                    }
                }
            }
            if (err_open) {
                long n = read(err_r, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    out_cb(cb_user, "stderr", buf);
                } else if (n == 0) {
                    err_open = 0;
                    close(err_r);
                } else {
                    if (*__errno_location() != 11 /* EAGAIN */) {
                        err_open = 0;
                        close(err_r);
                    }
                }
            }

            if (watch_poll(wfd)) {
                kill_child(pid);
                result->reloaded = 1;
                result->exit_code = 0;
                result->elapsed_ms = time_ms() - start;
                if (wfd >= 0) close(wfd);
                /* drain remaining output so the pipe doesn't stick */
                fcntl(out_r, F_SETFL, 0);
                fcntl(err_r, F_SETFL, 0);
                if (out_open) {
                    long n;
                    while ((n = read(out_r, buf, sizeof(buf) - 1)) > 0) {
                        buf[n] = '\0';
                        out_cb(cb_user, "stdout", buf);
                    }
                    close(out_r);
                }
                if (err_open) {
                    long n;
                    while ((n = read(err_r, buf, sizeof(buf) - 1)) > 0) {
                        buf[n] = '\0';
                        out_cb(cb_user, "stderr", buf);
                    }
                    close(err_r);
                }
                return result;
            }

            int wstatus = 0;
            int wr = waitpid(pid, &wstatus, WNOHANG);
            if (wr == pid) {
                if ((wstatus & 0x7f) == 0) {
                    result->exit_code = (wstatus >> 8) & 0xff;
                } else {
                    result->signal_num = wstatus & 0x7f;
                    result->exit_code = 128 + result->signal_num;
                }
                result->elapsed_ms = time_ms() - start;
                if (wfd >= 0) close(wfd);

                /* drain any remaining output (blocking) */
                fcntl(out_r, F_SETFL, 0);
                fcntl(err_r, F_SETFL, 0);
                if (out_open) {
                    long n;
                    while ((n = read(out_r, buf, sizeof(buf)-1)) > 0) {
                        buf[n] = '\0';
                        out_cb(cb_user, "stdout", buf);
                    }
                    close(out_r);
                }
                if (err_open) {
                    long n;
                    while ((n = read(err_r, buf, sizeof(buf)-1)) > 0) {
                        buf[n] = '\0';
                        out_cb(cb_user, "stderr", buf);
                    }
                    close(err_r);
                }
                return result;
            }

            usleep(2000); /* 2ms */
        }
    } else {
        /* no-capture path — optionally poll inotify so --watch can
           SIGTERM a long-running child (HTTP server) and relaunch. */
        int status = 0;
        if (wfd < 0) {
            waitpid(pid, &status, 0);
        } else {
            while (1) {
                if (watch_poll(wfd)) {
                    kill_child(pid);
                    result->reloaded = 1;
                    result->exit_code = 0;
                    result->elapsed_ms = time_ms() - start;
                    close(wfd);
                    return result;
                }
                int wr = waitpid(pid, &status, WNOHANG);
                if (wr == pid) break;
                usleep(2000);
            }
            close(wfd);
            wfd = -1;
        }
        result->elapsed_ms = time_ms() - start;
        if (wfd >= 0) close(wfd);

        if ((status & 0x7f) == 0) {
            result->exit_code = (status >> 8) & 0xff;
        } else {
            result->signal_num = status & 0x7f;
            result->exit_code  = 128 + result->signal_num;
        }
        return result;
    }

    /* unreachable */
    return result;
}

/* ═══════════════════════════════════════════════════════════════════════
   Compile — optionally compile .c to .bmir using classyc
   ═══════════════════════════════════════════════════════════════════════ */

int compile_source(char *source, char *output, char *compiler, int verbose) {
    int slen = strlen(compiler) + strlen(source) + strlen(output) + 32;
    char *cmd = (char *)malloc(slen);
    if (!cmd) return -1;

    sprintf(cmd, "%s -c -o %s %s 2>&1", compiler, output, source);
    if (verbose)
        printf("[jitrunner] compile: %s\n", cmd);

    int ret = system(cmd);
    free(cmd);

    if ((ret & 0x7f) == 0)
        ret = (ret >> 8) & 0xff;
    else
        ret = -1;

    return ret;
}

/* ═══════════════════════════════════════════════════════════════════════
   File watcher — inotify-based .bmir change detection
   ═══════════════════════════════════════════════════════════════════════ */

int wait_for_change(char *path, int verbose) {
    int ifd = inotify_init();
    if (ifd < 0) {
        fprintf(stderr, "[jitrunner] error: inotify_init failed\n");
        return 0;
    }

    int wd = inotify_add_watch(ifd, path, 0x002 | 0x008 | 0x080);
    if (wd < 0) {
        char *dir = (char *)malloc(strlen(path) + 1);
        strcpy(dir, path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
        } else {
            strcpy(dir, ".");
        }

        wd = inotify_add_watch(ifd, dir, 0x100 | 0x080);
        free(dir);
        if (wd < 0) {
            fprintf(stderr, "[jitrunner] error: cannot watch %s\n", path);
            close(ifd);
            return 0;
        }
    }

    if (verbose)
        printf("[jitrunner] watching %s for changes...\n", path);

    char buf[4096];
    long n = read(ifd, (void *)buf, 4096);

    close(ifd);

    if (n < 0) {
        if (*__errno_location() == 4 /* EINTR */) return 1;
        return 0;
    }

    return 1;
}

int wait_for_any_change(char **paths, int n, int verbose) {
    if (n <= 0 || !paths) return 0;
    if (n == 1) return wait_for_change(paths[0], verbose);
    int ifd = inotify_init();
    if (ifd < 0) {
        fprintf(stderr, "[jitrunner] error: inotify_init failed\n");
        return 0;
    }
    int added = 0;
    for (int i = 0; i < n; i++) {
        char *path = paths[i];
        if (!path || !path[0]) continue;
        int wd = inotify_add_watch(ifd, path, 0x002 | 0x008 | 0x080);
        if (wd < 0) {
            char *dir = (char *) malloc(strlen(path) + 1);
            strcpy(dir, path);
            char *slash = strrchr(dir, '/');
            if (slash) *slash = '\0';
            else strcpy(dir, ".");
            wd = inotify_add_watch(ifd, dir, 0x100 | 0x080);
            free(dir);
        }
        if (wd >= 0) added = added + 1;
        if (verbose && wd >= 0)
            printf("[jitrunner] watching %s for changes...\n", path);
    }
    if (!added) {
        fprintf(stderr, "[jitrunner] error: cannot watch any .bmir\n");
        close(ifd);
        return 0;
    }
    char buf[4096];
    long rn = read(ifd, (void *) buf, 4096);
    close(ifd);
    if (rn < 0) {
        if (*__errno_location() == 4) return 1;
        return 0;
    }
    return 1;
}

/* ═════════════════════════════════════════════════════════════════════
   Forward declarations for print helpers (used by dap_main)
   ═════════════════════════════════════════════════════════════════════ */

void print_run_result(RunResult *r);

/* ═════════════════════════════════════════════════════════════════════
   Include DAP server + DAP main loop
   ═════════════════════════════════════════════════════════════════════ */

#include "dap.h"

/* ═════════════════════════════════════════════════════════════════════
   DAP server main loop — entered when --dap <port> is given

   The jitrunner becomes a long-lived DAP server:
     1. Listen on the port
     2. Accept a client (Zed / VS Code connects)
     3. Handle initialize → configurationDone → launch handshake
     4. On launch: fork + JIT the .bmir, send output/exited/terminated
     5. On disconnect: close the client, go back to step 2
   ═══════════════════════════════════════════════════════════════════════ */

extern int open(char *path, int flags, ...);
#define O_WRONLY_J 1
#define O_CREAT_J 64
#define O_APPEND_J 1024
extern char *getenv(char *name);
extern int setenv(char *name, char *value, int overwrite);
extern int unsetenv(char *name);
/* Plain-C helper in mir-bridge.c (avoids File::write name clash) */
extern int dap_ctrl_write_byte(int fd, int byte);

/* Write a single resume command to the waiting debuggee child.
   cmd: 'c' continue, 's' stepIn, 'n' next/stepOver. */
static void dap_signal_child_resume(char cmd) {
    if (g_dap_ctrl_write_fd < 0) {
        if (dap_logger_fd >= 0)
            dprintf(dap_logger_fd, "dap_signal_child_resume: no ctrl write fd (cmd=%c)\n", cmd);
        return;
    }
    int rc = dap_ctrl_write_byte(g_dap_ctrl_write_fd, (int)cmd);
    if (dap_logger_fd >= 0)
        dprintf(dap_logger_fd, "dap_signal_child_resume cmd=%c fd=%d rc=%d\n",
                cmd, g_dap_ctrl_write_fd, rc);
}

/* Clear leftover breakpoint state at the start of a DAP session. */
static void dap_clear_bp_file(void) {
    FILE *f = fopen("/tmp/classyc-dap-bps.txt", "w");
    if (f) fclose(f);
}

/* True if /tmp/classyc-dap-bps.txt contains at least one breakpoint. */
static int dap_has_breakpoints(void) {
    FILE *bf = fopen("/tmp/classyc-dap-bps.txt", "r");
    if (!bf) return 0;
    char t[2];
    int has = (fread(t, 1, 1, bf) > 0) ? 1 : 0;
    fclose(bf);
    return has;
}

/* Map a DAP resume command name to the single-byte child control code. */
static char dap_resume_cmd_for(char *command) {
    if (!command) return 'c';
    if (strcmp(command, "next") == 0) return 'n';
    if (strcmp(command, "stepOver") == 0) return 'n';
    if (strcmp(command, "stepIn") == 0) return 's';
    /* stepOut: no full support yet — continue to next BP */
    return 'c';
}

/* Output callback used while a DAP session is running a program.
   Intercepts __DAP_BRK__ lines from the child, emits 'stopped',
   then blocks reading DAP until the client continues / steps / disconnects. */
static void dap_out_cb(void *u, char *cat, char *txt) {
    DapServer *d = (DapServer *)u;
    int is_step = (txt && strstr(txt, "__DAP_STEP__")) ? 1 : 0;
    int is_brk  = (txt && strstr(txt, "__DAP_BRK__"))  ? 1 : 0;
    if (is_step || is_brk) {
        d->record_stop_from_break_text(txt);
        /* Raw event — reason must be the correct DAP string */
        char json_buf[256];
        snprintf(json_buf, sizeof(json_buf),
            "{\"seq\":%d,\"type\":\"event\",\"event\":\"stopped\","
            "\"body\":{\"reason\":\"%s\",\"threadId\":1}}",
            d->next_seq(), is_step ? "step" : "breakpoint");
        dap_send_message(d->write_fp, json_buf);
        if (dap_logger_fd >= 0)
            dprintf(dap_logger_fd, "DAP stopped (%s) at %s:%d waiting for resume\n",
                    is_step ? "step" : "breakpoint",
                    (char *)d->stop_file, d->stop_line);
        while (1) {
            char *m = dap_read_message(d->client_fd);
            if (!m) {
                /* client gone — wake child so it can exit */
                dap_signal_child_resume('c');
                break;
            }
            /* Copy command name BEFORE dispatch/free — json() strings may
               alias into the message buffer and free(m) would UAF. */
            char cmd_copy[64];
            cmd_copy[0] = '\0';
            dict pp = json(m);
            if (pp != 0) {
                char *cc = (char *)pp.command;
                if (cc) {
                    int i = 0;
                    while (cc[i] && i < 63) {
                        cmd_copy[i] = cc[i];
                        i = i + 1;
                    }
                    cmd_copy[i] = '\0';
                }
            }
            int rr = d->dispatch_message(m);
            free(m);
            if (cmd_copy[0] && (strcmp(cmd_copy, "continue") == 0
                       || strcmp(cmd_copy, "next") == 0
                       || strcmp(cmd_copy, "stepIn") == 0
                       || strcmp(cmd_copy, "stepOver") == 0
                       || strcmp(cmd_copy, "stepOut") == 0)) {
                dap_signal_child_resume(dap_resume_cmd_for(cmd_copy));
                break;
            }
            if (rr == 1) { /* disconnect */
                dap_signal_child_resume('c');
                break;
            }
        }
        return;
    }
    d->send_output(cat, txt);
}

/* Run a .bmir under an active DAP session, optionally with breakpoints.
   Returns heap RunResult* (caller deletes).  Mutates cfg->jit_mode to
   interp when breakpoints are active. */
static RunResult *dap_run_program(DapServer *dap, RunConfig *cfg, char *bmir_path) {
    int ctrl_pipe[2];
    ctrl_pipe[0] = -1;
    ctrl_pipe[1] = -1;
    int has_bps = dap_has_breakpoints();
    if (dap_logger_fd >= 0)
        dprintf(dap_logger_fd, "DAP has_bps=%d mode=%d\n", has_bps, cfg->jit_mode);

    if (has_bps && pipe(ctrl_pipe) == 0) {
        /* Close write end in child / read end in parent is soft hygiene;
           we keep the full dual open across fork and just wire globals. */
        g_interp_child_ctrl_fd_from_env = ctrl_pipe[0];
        g_dap_ctrl_write_fd = ctrl_pipe[1];
        char fd_str[32];
        sprintf(fd_str, "%d", ctrl_pipe[0]);
        int r1 = setenv("CLASSYC_DEBUG_BPS", "/tmp/classyc-dap-bps.txt", 1);
        int r2 = setenv("CLASSYC_DEBUG_CTRL_FD", fd_str, 1);
        if (dap_logger_fd >= 0)
            dprintf(dap_logger_fd,
                    "DAP setenv BPS=%d CTRL=%d fd_str=%s read=%d write=%d\n",
                    r1, r2, fd_str, ctrl_pipe[0], ctrl_pipe[1]);
        /* Breakpoints require the MIR interpreter path */
        if (cfg->jit_mode != 2) cfg->jit_mode = 2;
    } else {
        unsetenv("CLASSYC_DEBUG_BPS");
        unsetenv("CLASSYC_DEBUG_CTRL_FD");
        g_interp_child_ctrl_fd_from_env = -1;
        g_dap_ctrl_write_fd = -1;
    }

    dap->on_pre_run(bmir_path);
    char *paths[JIT_MAX_BMIR + 1];
    int npaths = collect_bmirs(cfg, bmir_path, paths, JIT_MAX_BMIR + 1);
    RunResult *result = run_bmir(paths, npaths, cfg->jit_mode, cfg->verbose,
                                 dap_out_cb, (void *)dap, 0, 0);

    /* run_bmir parent path may already have closed the read end and
       cleared g_interp_child_ctrl_fd_from_env — avoid double-close. */
    if (ctrl_pipe[0] >= 0 && g_interp_child_ctrl_fd_from_env == ctrl_pipe[0])
        close(ctrl_pipe[0]);
    g_interp_child_ctrl_fd_from_env = -1;
    if (ctrl_pipe[1] >= 0) {
        if (g_dap_ctrl_write_fd == ctrl_pipe[1]) close(ctrl_pipe[1]);
        g_dap_ctrl_write_fd = -1;
    }
    unsetenv("CLASSYC_DEBUG_BPS");
    unsetenv("CLASSYC_DEBUG_CTRL_FD");
    dap->is_stopped = 0;
    return result;
}

int dap_main(RunConfig *cfg) {
    char *logp = getenv("CLASSYC_DAP_LOG");
    if (!logp && cfg->verbose) logp = "dapdebug.log";
    if (logp) { int fd=open(logp, O_WRONLY_J|O_CREAT_J|O_APPEND_J, 420); if(fd>=0){ dap_logger_fd=fd; dprintf(fd,"\n=== DAP start port=%d ===\n", cfg->dap_port);} }
    DapServer *dap = new DapServer(cfg->dap_port);
    defer delete dap;

    dap->verbose = cfg->verbose;
    dap_clear_bp_file();

    if (dap->start() < 0)
        return 1;

    /* Accept clients in a loop — the server stays alive */
    while (1) {
        if (dap->wait_for_client() < 0) {
            term_print_err("failed to accept DAP client");
            continue;
        }

        /* Fresh session: drop leftover breakpoints from a prior client */
        dap_clear_bp_file();
        dap->seq = 1;
        dap->is_stopped = 0;
        dap->launch_received = 0;
        dap->config_done_received = 0;

        /* ── DAP handshake: wait for launch AND configurationDone ──
           Order varies by client:
             Zed/VS Code: launch → setBreakpoints → configurationDone
             some tests:  configurationDone → launch
           Either way we must not start the debuggee early. */
        while (!dap->ready_to_run() && dap->state != 5) {
            char *msg = dap_read_message(dap->client_fd);
            if (!msg) {
                term_print_warn("DAP client disconnected during handshake");
                break;
            }

            int result = dap->dispatch_message(msg);
            free(msg);

            if (result < 0 || result == 1)
                break;  /* error or disconnect */
        }

        if (!dap->ready_to_run()) {
            /* Client disconnected before launch+configDone — reset and wait */
            dap->state = 0;
            continue;
        }

        /* ── Run the program ─────────────────────────────────────── */
        char *bmir_path = (char *)dap->program_path;
        /* Fall back to CLI-provided path if launch omitted "program" */
        if ((!bmir_path || strlen(bmir_path) == 0) && strlen(cfg->bmir_path) > 0) {
            dap->program_path = cfg->bmir_path;
            bmir_path = (char *)dap->program_path;
        }

        /* If program_path is empty, nothing to run */
        if (!bmir_path || strlen(bmir_path) == 0) {
            dap->send_output("stderr",
                "[jitrunner] error: no program specified in launch request\n");
            dict exit_body = {"exitCode": 1};

            dap->send_event("exited", exit_body);
            dap->send_event_simple("terminated");
        } else if (!File.exists(bmir_path)) {
            String errmsg = f"[jitrunner] error: {bmir_path} not found\n";
            dap->send_output("stderr", (char *)errmsg);
            dict exit_body2 = {"exitCode": 1};

            dap->send_event("exited", exit_body2);
            dap->send_event_simple("terminated");
        } else {
            RunResult *result = dap_run_program(dap, cfg, bmir_path);
            print_run_result(result);
            dap->on_post_run(result->exit_code, result->signal_num);
            delete result;
        }

        /* ── Post-run: drain remaining messages (disconnect, etc.) ── */
        while (dap->state != 5) {
            char *msg = dap_read_message(dap->client_fd);
            if (!msg) break;

            int result = dap->dispatch_message(msg);
            free(msg);

            if (result < 0 || result == 1) break;
        }

        /* Reset for next session */
        if (dap->write_fp) {
            fclose(dap->write_fp);
            dap->write_fp = (FILE *)0;
            /* fclose also closes the underlying fd, so skip tcp_close */
            dap->client_fd = -1;
        } else if (dap->client_fd >= 0) {
            tcp_close(dap->client_fd);
            dap->client_fd = -1;
        }
        dap->state = 0;
        dap->seq = 1;
        dap->program_path = "";

        term_print_info("DAP session ended, waiting for next client...");
        printf("\n");
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
   CLI parsing
   ═══════════════════════════════════════════════════════════════════════ */

void print_usage(void) {
    printf("classyc jitrunner \xe2\x80\x94 hot-reload .bmir runner\n\n");
    printf("Usage:\n");
    printf("  jitrunner <file.bmir> [options]\n");
    printf("  jitrunner <lib.bmir ...> <app.bmir> [options]\n");
    printf("  jitrunner --compile <file.c> -o <file.bmir> [options]\n");
    printf("  jitrunner --dap <port>                       (DAP server)\n");
    printf("  jitrunner --dap-stdio                        (DAP over stdin/stdout)\n\n");
    printf("  Multiple .bmir files are loaded into one MIR context and linked\n");
    printf("  at run time (last main() wins). --watch SIGTERMs the child when\n");
    printf("  any of those files change and relaunches — hot-reload for a\n");
    printf("  long-running server. A DAP restart can use the same path later.\n\n");
    printf("Options:\n");
    printf("  --watch, -w        Watch .bmir(s) and re-run on change\n");
    printf("  --mode <m>         JIT mode: lazy (default), gen, interp\n");
    printf("  --compile <file.c> Compile .c to .bmir before running\n");
    printf("  --compiler <path>  Path to classyc (default: ./bin/classyc)\n");
    printf("  -o <file.bmir>     Output path for compiled .bmir\n");
    printf("  --dap <port>       Start as DAP debug server on <port>\n");
    printf("  --dap-stdio        DAP over stdin/stdout (for editor integration)\n");
    printf("  --verbose, -v      Verbose output\n");
    printf("  --help, -h         Show this help\n");
}

RunConfig *parse_args(int argc, char **argv) {
    RunConfig *cfg = new RunConfig();

    int i = 1;
    while (i < argc) {
        char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            delete cfg;
            return 0;
        }

        if (strcmp(arg, "--watch") == 0 || strcmp(arg, "-w") == 0) {
            cfg->watch = 1;
            i = i + 1;
            continue;
        }

        if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
            cfg->verbose = 1;
            i = i + 1;
            continue;
        }

        if (strcmp(arg, "--mode") == 0) {
            i = i + 1;
            if (i >= argc) {
                fprintf(stderr, "error: --mode requires an argument\n");
                delete cfg;
                return 0;
            }
            if (strcmp(argv[i], "gen") == 0)
                cfg->jit_mode = 1;
            else if (strcmp(argv[i], "interp") == 0)
                cfg->jit_mode = 2;
            else
                cfg->jit_mode = 0;
            i = i + 1;
            continue;
        }

        if (strcmp(arg, "--compile") == 0) {
            i = i + 1;
            if (i >= argc) {
                fprintf(stderr, "error: --compile requires a .c path\n");
                delete cfg;
                return 0;
            }
            cfg->source_path = argv[i];
            i = i + 1;
            continue;
        }

        if (strcmp(arg, "--compiler") == 0) {
            i = i + 1;
            if (i >= argc) {
                fprintf(stderr, "error: --compiler requires a path\n");
                delete cfg;
                return 0;
            }
            cfg->compiler_path = argv[i];
            i = i + 1;
            continue;
        }

        if (strcmp(arg, "-o") == 0) {
            i = i + 1;
            if (i >= argc) {
                fprintf(stderr, "error: -o requires a path\n");
                delete cfg;
                return 0;
            }
            cfg->add_bmir(argv[i]);
            i = i + 1;
            continue;
        }

        if (strcmp(arg, "--dap") == 0) {
            i = i + 1;
            if (i >= argc) {
                fprintf(stderr, "error: --dap requires a port number\n");
                delete cfg;
                return 0;
            }
            cfg->dap_port = atoi(argv[i]);
            if (cfg->dap_port <= 0) {
                fprintf(stderr, "error: --dap port must be > 0\n");
                delete cfg;
                return 0;
            }
            i = i + 1;
            continue;
        }

        if (strcmp(arg, "--dap-stdio") == 0) {
            cfg->dap_stdio = 1;
            i = i + 1;
            continue;
        }

        /* positional: another .bmir (libs first, app last) */
        cfg->add_bmir(arg);

        i = i + 1;
    }

    return cfg;
}

/* ═════════════════════════════════════════════════════════════════════
   DAP stdio main — entered when --dap-stdio is given

   Zed (or any editor) launches this process and speaks DAP over
   stdin/stdout.  We dup stdout to a saved fd so forked children
   don't corrupt the DAP channel (their printf goes to stderr).
   ═════════════════════════════════════════════════════════════════════ */

int dap_stdio_main(RunConfig *cfg) {
    /* Prefer CLASSYC_DAP_LOG; open only if not already opened by main(). */
    if (dap_logger_fd < 0) {
        char *logp2 = getenv("CLASSYC_DAP_LOG");
        if (!logp2 && cfg->verbose) logp2 = "dapdebug.log";
        if (logp2) {
            int fd = open(logp2, O_WRONLY_J | O_CREAT_J | O_APPEND_J, 420);
            if (fd >= 0) {
                dap_logger_fd = fd;
                dprintf(fd, "\n=== DAP stdio start ===\n");
            }
        }
    } else {
        dprintf(dap_logger_fd, "\n=== DAP stdio start ===\n");
    }

    /* Save original stdout for DAP output, then redirect stdout
       to stderr so any printf/child output doesn't corrupt DAP. */
    int dap_out_fd = dup(1);
    dup2(2, 1);

    FILE *dap_out = fdopen(dap_out_fd, "w");
    if (!dap_out) {
        fprintf(stderr, "[jitrunner] error: fdopen failed for DAP output\n");
        return 1;
    }

    DapServer *dap = new DapServer(0);
    dap->verbose = cfg->verbose;
    dap->init_stdio(dap_out);
    dap_clear_bp_file();
    dap->launch_received = 0;
    dap->config_done_received = 0;

    /* DAP handshake: wait for BOTH launch and configurationDone.
       Clients set breakpoints between these; starting early skips them. */
    while (!dap->ready_to_run() && dap->state != 5) {
        char *msg = dap_read_message(0);  /* read from stdin */
        if (!msg) break;

        int result = dap->dispatch_message(msg);
        free(msg);

        if (result < 0 || result == 1) break;
    }

    if (dap_logger_fd >= 0)
        dprintf(dap_logger_fd,
                "stdio handshake done: ready=%d launch=%d configDone=%d state=%d\n",
                dap->ready_to_run(), dap->launch_received,
                dap->config_done_received, dap->state);

    int exit_code = 0;

    if (!dap->ready_to_run()) {
        exit_code = 1;
    } else {
        /* Run the program */
        char *bmir_path = (char *)dap->program_path;
        if ((!bmir_path || strlen(bmir_path) == 0) && strlen(cfg->bmir_path) > 0) {
            dap->program_path = cfg->bmir_path;
            bmir_path = (char *)dap->program_path;
        }

        if (!bmir_path || strlen(bmir_path) == 0) {
            dap->send_output("stderr",
                "[jitrunner] error: no program specified in launch request\n");
            dict exit_body = {"exitCode": 1};

            dap->send_event("exited", exit_body);
            dap->send_event_simple("terminated");
            exit_code = 1;
        } else if (!File.exists(bmir_path)) {
            String errmsg = f"[jitrunner] error: {bmir_path} not found\n";
            dap->send_output("stderr", (char *)errmsg);
            dict exit_body2 = {"exitCode": 1};

            dap->send_event("exited", exit_body2);
            dap->send_event_simple("terminated");
            exit_code = 1;
        } else {
            RunResult *result = dap_run_program(dap, cfg, bmir_path);
            dap->on_post_run(result->exit_code, result->signal_num);
            exit_code = result->exit_code;
            delete result;
        }
    }

    /* Drain remaining messages (disconnect, etc.) */
    while (dap->state != 5) {
        char *msg = dap_read_message(0);
        if (!msg) break;
        int r = dap->dispatch_message(msg);
        free(msg);
        if (r < 0 || r == 1) break;
    }

    /* Clean up without closing stdin/dap_out through the destructor */
    dap->client_fd = -1;
    dap->write_fp  = (FILE *)0;
    delete dap;

    fclose(dap_out);
    return exit_code;
}

/* ═════════════════════════════════════════════════════════════════════
   Pretty output helpers
   ═════════════════════════════════════════════════════════════════════ */

void print_banner(void) {
    term_box("classyc jitrunner \xe2\x80\x94 hot-reload .bmir");
    printf("\n");
}

void print_run_start(char **paths, int npaths, int run_num) {
    char label[64];
    sprintf(label, "run #%d", run_num);
    term_hr_label(label);
    if (npaths <= 1)
        printf("  file: %s\n", npaths == 1 ? paths[0] : "");
    else {
        printf("  files:\n");
        for (int i = 0; i < npaths; i++)
            printf("         %s\n", paths[i]);
    }
}

void print_run_result(RunResult *r) {
    if (r->reloaded)
        term_print_info("file changed, reloading...");
    else if (r->ok())
        term_print_ok((char *)r->summary());
    else
        term_print_err((char *)r->summary());
}

/* ═══════════════════════════════════════════════════════════════════════
   Main
   ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    RunConfig *cfg = parse_args(argc, argv);
    if (!cfg) return 1;
    defer delete cfg;

    /* ── DAP stdio mode (for Zed / editor integration) ─────────── */
    /* Skip the banner — stdout is the DAP channel, not a terminal. */

    if (cfg->dap_stdio) {
        /* Optional debug log for wire traffic.  Always-on default path keeps
           editor sessions inspectable; override/disable with CLASSYC_DAP_LOG="".
           Use the same open flags as the rest of the DAP code. */
        char *logp = getenv("CLASSYC_DAP_LOG");
        if (!logp) logp = "dapdebug.log";
        if (logp[0]) {
            dap_logger_fd = open(logp, O_WRONLY_J | O_CREAT_J | O_APPEND_J, 0644);
            if (dap_logger_fd < 0) dap_logger_fd = -1;
        }
        return dap_stdio_main(cfg);
    }

    print_banner();

    /* ── DAP TCP server mode ───────────────────────────────────── */
    if (cfg->dap_port > 0) {
        printf("  mode:  DAP server\n");
        printf("  port:  %d\n", cfg->dap_port);
        printf("  jit:   %s\n\n",
               cfg->jit_mode == 1 ? "gen" :
               cfg->jit_mode == 2 ? "interp" : "lazy");
        return dap_main(cfg);
    }

    /* ── CLI mode (original behaviour) ───────────────────────────── */

    /* If --compile was given, figure out bmir_path from source */
    if (strlen(cfg->source_path) > 0 && cfg->nbmirs == 0) {
        int slen = strlen(cfg->source_path);
        char *out = (char *)malloc(slen + 6);
        strcpy(out, cfg->source_path);
        if (slen > 2 && strcmp(out + slen - 2, ".c") == 0)
            out[slen - 2] = '\0';
        else if (slen > 3 && strcmp(out + slen - 3, ".cy") == 0)
            out[slen - 3] = '\0';
        strcat(out, ".bmir");
        cfg->add_bmir(out);
    }

    if (cfg->nbmirs == 0) {
        fprintf(stderr, "error: no .bmir file specified\n");
        print_usage();
        return 1;
    }

    String mode_names[3] = {"lazy", "gen", "interp"};
    printf("  mode:  %s\n", mode_names[cfg->jit_mode]);
    printf("  watch: %s\n", cfg->watch ? "on" : "off");

    if (strlen(cfg->source_path) > 0)
        printf("  source: %s\n", cfg->source_path);

    if (cfg->nbmirs == 1)
        printf("  bmir:  %s\n\n", cfg->bmir_paths[0]);
    else {
        printf("  bmir:\n");
        for (int i = 0; i < cfg->nbmirs; i++)
            printf("         %s\n", cfg->bmir_paths[i]);
        printf("\n");
    }

    int run_num = 0;

    /* ── Main run loop ───────────────────────────────────────────── */
    do {
        /* Compile if needed */
        if (strlen(cfg->source_path) > 0) {
            printf("[jitrunner] compiling %s...\n", cfg->source_path);
            int cret = compile_source(
                (char *)cfg->source_path,
                (char *)cfg->bmir_path,
                (char *)cfg->compiler_path,
                cfg->verbose
            );
            if (cret != 0) {
                char errmsg[128];
                sprintf(errmsg, "compile failed (exit %d)", cret);
                term_print_err(errmsg);
                if (!cfg->watch) return 1;
                char *watch_path = (char *)cfg->source_path;
                wait_for_change(watch_path, cfg->verbose);
                continue;
            }
        }

        /* Check every .bmir exists */
        int missing = 0;
        for (int i = 0; i < cfg->nbmirs; i++) {
            if (!File.exists(cfg->bmir_paths[i])) {
                fprintf(stderr, "[jitrunner] error: %s not found\n",
                        cfg->bmir_paths[i]);
                missing = 1;
            }
        }
        if (missing) {
            if (!cfg->watch) return 1;
            wait_for_any_change(cfg->bmir_paths, cfg->nbmirs, cfg->verbose);
            continue;
        }

        run_num = run_num + 1;
        print_run_start(cfg->bmir_paths, cfg->nbmirs, run_num);

        /* Watch list: all .bmirs, plus source if we're compiling. */
        char *watch_paths[JIT_MAX_BMIR + 1];
        int nwatch = 0;
        if (cfg->watch) {
            for (int i = 0; i < cfg->nbmirs; i++) {
                watch_paths[nwatch] = cfg->bmir_paths[i];
                nwatch = nwatch + 1;
            }
            if (strlen(cfg->source_path) > 0) {
                watch_paths[nwatch] = (char *) cfg->source_path;
                nwatch = nwatch + 1;
            }
        }

        /* Fork + JIT + run. With --watch, a file change SIGTERMs the
           child (hot-reload) instead of waiting for it to exit. */
        RunResult *result = run_bmir(
            cfg->bmir_paths, cfg->nbmirs,
            cfg->jit_mode,
            cfg->verbose,
            NULL, NULL,
            cfg->watch ? watch_paths : 0,
            cfg->watch ? nwatch : 0
        );

        int reloaded = result->reloaded;
        int last_exit = result->exit_code;
        print_run_result(result);
        delete result;

        if (!cfg->watch)
            return last_exit;

        if (!reloaded) {
            /* Child exited on its own — wait for the next edit. */
            printf("\n");
            int changed = wait_for_any_change(watch_paths, nwatch, cfg->verbose);
            if (!changed) {
                fprintf(stderr, "[jitrunner] watch error, exiting\n");
                return 1;
            }
            term_print_info("file changed, reloading...");
            printf("\n");
        }

        usleep(100 * 1000);

    } while (cfg->watch);

    return 0;
}
