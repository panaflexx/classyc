/* jitrunner.c — ClassyC hot-reload runner for .bmir files
 *
 * A dogfood program written in ClassyC that:
 *   1. Loads a .bmir file and JIT-runs its main() in a forked child
 *   2. Watches the file for changes (inotify on Linux)
 *   3. On change, kills any running child and re-runs
 *   4. Optionally acts as a DAP server for IDE debugging (--dap <port>)
 *
 * Build (from project root):
 *   bash src/jitrunner/build.sh
 *
 * Usage:
 *   bin/jitrunner path/to/program.bmir [--watch] [--mode lazy|gen|interp]
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

/* ═══════════════════════════════════════════════════════════════════════
   RunConfig — parsed command-line options
   ═══════════════════════════════════════════════════════════════════════ */

class RunConfig {
    String bmir_path;       /* path to the .bmir file to run          */
    String source_path;     /* optional: .c source to compile first   */
    String compiler_path;   /* path to classyc compiler               */
    int    watch;           /* 1 = watch for changes and re-run       */
    int    jit_mode;        /* 0=lazy  1=gen  2=interp                */
    int    verbose;         /* 1 = extra diagnostic output            */
    int    dap_port;        /* >0 = run as DAP server on this port    */
    int dap_stdio;       /* 1 = DAP over stdin/stdout (for Zed)    */

    RunConfig() {
        this.bmir_path     = "";
        this.source_path   = "";
        this.compiler_path = "./bin/classyc";
        this.watch         = 0;
        this.jit_mode      = 0;
        this.verbose       = 0;
        this.dap_port      = 0;
        this.dap_stdio     = 0;
    }

    ~RunConfig() {}
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
    long elapsed_ms;       /* wall-clock milliseconds                 */

    RunResult() {
        this.exit_code  = -1;
        this.signal_num = 0;
        this.timed_out  = 0;
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

long time_ms(void) {
    long buf[2];  /* tv_sec, tv_nsec */
    buf[0] = 0;
    buf[1] = 0;
    clock_gettime(0 /* CLOCK_REALTIME */, (void *)buf);
    return buf[0] * 1000 + buf[1] / 1000000;
}

/* ═══════════════════════════════════════════════════════════════════════
   JIT execution — fork + JIT in child
   ═══════════════════════════════════════════════════════════════════════ */

RunResult *run_bmir(char *bmir_path, int jit_mode, int verbose, OutputCB out_cb, void *cb_user) {
    RunResult *result = new RunResult();
    long start = time_ms();

    if (verbose)
        printf("[jitrunner] loading %s (mode=%d)\n", bmir_path, jit_mode);

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

    int pid = fork();

    if (pid < 0) {
        printf("[jitrunner] error: fork() failed\n");
        if (out_cb) { close(out_pipe[0]); close(out_pipe[1]); close(err_pipe[0]); close(err_pipe[1]); }
        result->exit_code = 127;
        result->elapsed_ms = time_ms() - start;
        return result;
    }

    if (pid == 0) {
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

        if (jit_read_bmir(ctx, bmir_path) != 0) {
            fprintf(stderr, "[jitrunner] error: cannot read %s\n", bmir_path);
            jit_finish(ctx);
            _exit(125);
        }

        JIT_item main_func = 0;
        JIT_module mod = jit_first_module(ctx);
        while (mod) {
            JIT_item item = jit_first_item(mod);
            while (item) {
                if (jit_item_is_func(item)) {
                    char *name = jit_func_name(item);
                    if (name && strcmp(name, "main") == 0)
                        main_func = item;
                }
                item = jit_next_item(item);
            }
            jit_load_module(ctx, mod);
            mod = jit_next_module(mod);
        }

        if (!main_func) {
            fprintf(stderr, "[jitrunner] error: no main() found in %s\n",
                    bmir_path);
            jit_finish(ctx);
            _exit(124);
        }

        jit_gen_init(ctx);
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
    int out_r = out_pipe[0];
    int err_r = err_pipe[0];

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
        /* original no-capture path */
        int status = 0;
        waitpid(pid, &status, 0);
        result->elapsed_ms = time_ms() - start;

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

int dap_main(RunConfig *cfg) {
    DapServer *dap = new DapServer(cfg->dap_port);
    defer delete dap;

    dap->verbose = cfg->verbose;

    if (dap->start() < 0)
        return 1;

    /* Accept clients in a loop — the server stays alive */
    while (1) {
        if (dap->wait_for_client() < 0) {
            term_print_err("failed to accept DAP client");
            continue;
        }

        /* ── DAP handshake: process messages until we get 'launch' ── */
        int launched = 0;
        while (!launched && dap->state != 5) {
            char *msg = dap_read_message(dap->client_fd);
            if (!msg) {
                term_print_warn("DAP client disconnected during handshake");
                break;
            }

            /* Peek at the command to detect 'launch' */
            dict parsed = json(msg);
            char *command = (char *)0;
            if (parsed != 0)
                command = (char *)parsed.command;

            int result = dap->dispatch_message(msg);
            free(msg);

            if (result < 0 || result == 1)
                break;  /* error or disconnect */

            /* After 'launch' is handled, the program path is set */
            if (command && strcmp(command, "launch") == 0)
                launched = 1;
        }

        if (!launched) {
            /* Client disconnected before launching — reset and wait */
            dap->state = 0;
            continue;
        }

        /* ── Run the program ─────────────────────────────────────── */
        char *bmir_path = (char *)dap->program_path;

        /* If program_path is empty, nothing to run */
        if (!bmir_path || strlen(bmir_path) == 0) {
            dap->send_output("stderr",
                "[jitrunner] error: no program specified in launch request\n");
            dict exit_body = {"exitCode": 1};

            dap->send_event("exited", exit_body);
            dap->send_event_simple("terminated");
        } else {
            /* Check the file exists */
            if (!File.exists(bmir_path)) {
                String errmsg = f"[jitrunner] error: {bmir_path} not found\n";
                dap->send_output("stderr", (char *)errmsg);
                dict exit_body2 = {"exitCode": 1};

                dap->send_event("exited", exit_body2);
                dap->send_event_simple("terminated");
            } else {
                dap->on_pre_run(bmir_path);

                /* Fork + JIT + run */
                RunResult *result = run_bmir(bmir_path, cfg->jit_mode, cfg->verbose,
                    (void *u, char *cat, char *txt) => { ((DapServer*)u)->send_output(cat, txt); },
                    (void *)dap);

                /* Print locally too */
                print_run_result(result);

                /* Send DAP events */
                dap->on_post_run(result->exit_code, result->signal_num);

                delete result;
            }
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
    printf("  jitrunner --compile <file.c> -o <file.bmir> [options]\n");
    printf("  jitrunner --dap <port>                       (DAP server)\n");
    printf("  jitrunner --dap-stdio                        (DAP over stdin/stdout)\n\n");
    printf("Options:\n");
    printf("  --watch, -w        Watch file and re-run on change\n");
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
            cfg->bmir_path = argv[i];
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

        /* positional: .bmir path */
        if (strlen(cfg->bmir_path) == 0)
            cfg->bmir_path = arg;

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

    /* DAP handshake: process messages until we get 'launch' */
    int launched = 0;
    while (!launched && dap->state != 5) {
        char *msg = dap_read_message(0);  /* read from stdin */
        if (!msg) break;

        dict parsed = json(msg);
        char *command = (char *)0;
        if (parsed != 0)
            command = (char *)parsed.command;

        int result = dap->dispatch_message(msg);
        free(msg);

        if (result < 0 || result == 1) break;

        if (command && strcmp(command, "launch") == 0)
            launched = 1;
    }

    int exit_code = 0;

    if (!launched) {
        exit_code = 1;
    } else {
        /* Run the program */
        char *bmir_path = (char *)dap->program_path;

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
            dap->on_pre_run(bmir_path);
            RunResult *result = run_bmir(bmir_path, cfg->jit_mode, cfg->verbose,
                (void *u, char *cat, char *txt) => { ((DapServer*)u)->send_output(cat, txt); },
                (void *)dap);
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

void print_run_start(char *path, int run_num) {
    char label[64];
    sprintf(label, "run #%d", run_num);
    term_hr_label(label);
    printf("  file: %s\n", path);
}

void print_run_result(RunResult *r) {
    if (r->ok())
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
        /* Open a debug log for all DAP wire traffic (both directions) */
        dap_logger_fd = open("dapdebug.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (dap_logger_fd < 0) {
            /* non-fatal: continue without logging */
            dap_logger_fd = -1;
        } else {
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
    if (strlen(cfg->source_path) > 0 && strlen(cfg->bmir_path) == 0) {
        int slen = strlen(cfg->source_path);
        char *out = (char *)malloc(slen + 6);
        strcpy(out, cfg->source_path);
        if (slen > 2 && strcmp(out + slen - 2, ".c") == 0)
            out[slen - 2] = '\0';
        strcat(out, ".bmir");
        cfg->bmir_path = out;
    }

    if (strlen(cfg->bmir_path) == 0) {
        fprintf(stderr, "error: no .bmir file specified\n");
        print_usage();
        return 1;
    }

    String mode_names[3] = {"lazy", "gen", "interp"};
    printf("  mode:  %s\n", mode_names[cfg->jit_mode]);
    printf("  watch: %s\n", cfg->watch ? "on" : "off");

    if (strlen(cfg->source_path) > 0)
        printf("  source: %s\n", cfg->source_path);

    printf("  bmir:  %s\n\n", cfg->bmir_path);

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

        /* Check the .bmir file exists */
        if (!File.exists((char *)cfg->bmir_path)) {
            fprintf(stderr, "[jitrunner] error: %s not found\n",
                    (char *)cfg->bmir_path);
            if (!cfg->watch) return 1;
            wait_for_change((char *)cfg->bmir_path, cfg->verbose);
            continue;
        }

        run_num = run_num + 1;
        print_run_start((char *)cfg->bmir_path, run_num);

        /* Fork + JIT + run */
        RunResult *result = run_bmir(
            (char *)cfg->bmir_path,
            cfg->jit_mode,
            cfg->verbose,
            NULL, NULL
        );

        print_run_result(result);

        int last_exit = result->exit_code;
        delete result;

        if (!cfg->watch)
            return last_exit;

        /* Watch for changes */
        printf("\n");

        char *watch_target = (char *)cfg->bmir_path;
        if (strlen(cfg->source_path) > 0)
            watch_target = (char *)cfg->source_path;

        int changed = wait_for_change(watch_target, cfg->verbose);
        if (!changed) {
            fprintf(stderr, "[jitrunner] watch error, exiting\n");
            return 1;
        }

        term_print_info("file changed, reloading...");
        printf("\n");

        usleep(100 * 1000);

    } while (cfg->watch);

    return 0;
}
