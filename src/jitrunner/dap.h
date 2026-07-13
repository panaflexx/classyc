/* =========================================================================
   dap.h — Debug Adapter Protocol server for the ClassyC JIT runner
   =========================================================================

   Implements the DAP wire protocol (Content-Length framing + JSON) over TCP,
   using include/tcp.h for sockets and ClassyC's dict/json() for messages.

   DAP wire format (identical to LSP):
     Content-Length: <N>\r\n
     \r\n
     <N bytes of JSON>
   ========================================================================= */

#ifndef DAP_H
#define DAP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "include/tcp.h"
#include "include/term.h"
//#include "include/dict.h"

/* ── POSIX I/O declarations for stdio DAP mode ───────────────────── */
extern long read(int fd, void *buf, long count);
extern long time(long *tloc);   /* time(2) for log timestamps */

/* Global DAP debug log file descriptor.  Opened by main (e.g. to
   "dapdebug.log").  If >= 0, all inbound/outbound DAP messages
   are appended to it for inspection. */
extern int dap_logger_fd;

/* Transport-agnostic I/O helpers.
   Using read() + fwrite()/fflush() so we work with both sockets
   and stdin/stdout (avoids declaring POSIX write() which clashes
   with File::write in ClassyC).                                    */

static int dap_io_read(int fd, char *buf, int len) {
    long n = read(fd, (void *)buf, (long)len);
    return (int)n;
}

static int dap_io_read_exact(int fd, char *buf, int len) {
    int total = 0;
    while (total < len) {
        long n = read(fd, (void *)(buf + total), (long)(len - total));
        if (n <= 0) return -1;
        total = total + (int)n;
    }
    return total;
}

static int dap_io_fwrite(FILE *f, char *buf, int len) {
    int n = (int)fwrite(buf, 1, (size_t)len, f);
    fflush(f);
    return (n == len) ? len : -1;
}

/* ══════════════════════════════════════════════════════════════════════
   DAP message framing — Content-Length protocol
   (Adapted from nanoproxy's http.h header-parsing pattern)
   ══════════════════════════════════════════════════════════════════════ */

/* Read one DAP message from `fd`.  Returns a malloc'd JSON string,
   or NULL on disconnect/error.  Caller must free(). */
static char *dap_read_message(int fd) {
    char header_buf[512];
    int hpos = 0;

    while (hpos < 510) {
        char c;
        int n = dap_io_read(fd, &c, 1);
        if (n <= 0) return (char *)0;

        header_buf[hpos] = c;
        hpos = hpos + 1;

        if (hpos >= 4
            && header_buf[hpos - 4] == '\r'
            && header_buf[hpos - 3] == '\n'
            && header_buf[hpos - 2] == '\r'
            && header_buf[hpos - 1] == '\n') {
            header_buf[hpos] = '\0';
            break;
        }
    }

    char *cl = strstr(header_buf, "Content-Length:");
    if (!cl) cl = strstr(header_buf, "content-length:");
    if (!cl) return (char *)0;

    int content_length = atoi(cl + 15);
    if (content_length <= 0 || content_length > 1024 * 1024) return (char *)0;

    char *body = (char *)malloc(content_length + 1);
    if (!body) return (char *)0;

    if (dap_io_read_exact(fd, body, content_length) < 0) {
        free(body);
        return (char *)0;
    }
    body[content_length] = '\0';

    if (dap_logger_fd >= 0) {
        dprintf(dap_logger_fd, "\n===== IN  %ld =====\n%s\n", (long)time(0), body);
    }
    return body;
}

/* Send a DAP message (JSON string) with Content-Length framing.
   Uses FILE* so we can write to both sockets (fdopen'd) and stdout. */
static int dap_send_message(FILE *out, char *json_str) {
    int body_len = (int)strlen(json_str);
    char header[128];
    int hlen = sprintf(header, "Content-Length: %d\r\n\r\n", body_len);

    if (dap_io_fwrite(out, header, hlen) < 0) return -1;
    if (dap_io_fwrite(out, json_str, body_len) < 0) return -1;

    if (dap_logger_fd >= 0) {
        dprintf(dap_logger_fd, f"\n===== OUT t={time(0)} len={body_len} =====\n{json_str}\n");
    }
    return 0;
}

/* Send a dict as a DAP message via its .json() serialization. */
static int dap_send_dict(FILE *out, dict msg) {
    char *j = msg.json();
    if (!j) return -1;
    return dap_send_message(out, j);
}

/* ══════════════════════════════════════════════════════════════════════
   DAP Server class
   ══════════════════════════════════════════════════════════════════════ */

class DapServer {
    int   listen_fd;
    int   client_fd;   /* fd used for reading DAP messages          */
    FILE *write_fp;    /* FILE* used for writing DAP messages       */
    int   port;
    int   seq;
    int   state;       /* 0=idle 1=initializing 2=configured 3=running 5=terminated */
    int   verbose;
    int   stdio_p;     /* 1 = stdin/stdout mode (no TCP)            */
    String program_path;
    /* Last stop location (from __DAP_BRK__ child notification) */
    String stop_file;
    int    stop_line;
    int    stop_col;
    int    is_stopped;

    DapServer(int port) {
        this.listen_fd    = -1;
        this.client_fd    = -1;
        this.write_fp     = (FILE *)0;
        this.port         = port;
        this.seq          = 1;
        this.state        = 0;
        this.verbose      = 0;
        this.stdio_p      = 0;
        this.program_path = "";
        this.stop_file    = "";
        this.stop_line    = 0;
        this.stop_col     = 0;
        this.is_stopped   = 0;
    }

    ~DapServer() {
        if (!this.stdio_p) {
            if (this.write_fp) { fclose(this.write_fp); this.write_fp = (FILE *)0; }
            if (this.client_fd >= 0) { tcp_close(this.client_fd); this.client_fd = -1; }
            if (this.listen_fd >= 0) { tcp_close(this.listen_fd); this.listen_fd = -1; }
        }
    }

    int start() {
        this.listen_fd = tcp_listen("127.0.0.1", this.port, 1);
        if (this.listen_fd < 0) {
            fprintf(stderr, "[DAP] failed to listen on port %d\n", this.port);
            return -1;
        }
        if (this.port == 0)
            this.port = tcp_bound_port(this.listen_fd);

        term_print_info("DAP server listening");
        printf("  port: %d\n", this.port);
        return 0;
    }

    void stop() {
        if (this.client_fd >= 0) {
            tcp_close(this.client_fd);
            this.client_fd = -1;
        }
        if (this.listen_fd >= 0) {
            tcp_close(this.listen_fd);
            this.listen_fd = -1;
        }
    }

    int wait_for_client() {
        if (this.listen_fd < 0) return -1;
        term_print_info("waiting for DAP client...");
        this.client_fd = tcp_accept(this.listen_fd);
        if (this.client_fd < 0) return -1;
        this.write_fp = fdopen(this.client_fd, "w");
        if (!this.write_fp) { tcp_close(this.client_fd); this.client_fd = -1; return -1; }
        term_print_ok("DAP client connected");
        return 0;
    }

    /* Set up for stdin/stdout DAP (no TCP).  `out_fp` is the FILE*
       wrapping the original stdout (caller should fdopen/dup beforehand). */
    void init_stdio(FILE *out_fp) {
        this.client_fd = 0;        /* read DAP from stdin */
        this.write_fp  = out_fp;   /* write DAP to saved stdout */
        this.stdio_p   = 1;
    }

    int next_seq() {
        int s = this.seq;
        this.seq = this.seq + 1;
        return s;
    }

    /* ── Send helpers ────────────────────────────────────────────── */

    void send_event_simple(char *event_name) {
            if (!this.write_fp) return;
            dict ev = { "seq": this->next_seq(), "type": "event", "event": "" };
            ev.event = event_name;
            dap_send_dict(this.write_fp, ev);
        }

    void send_event(char *event_name, dict body) {
            if (!this.write_fp) return;
            dict ev = { "seq": this->next_seq(), "type": "event", "event": "" };
            ev.event = event_name;
            ev.body = body;
            dap_send_dict(this.write_fp, ev);
        }

    void send_output(char *category, char *text) {
        if (!this.write_fp) return;
        dict body = { "category": "", "output": "" };
        body.category = category;
        body.output = text;
        this->send_event("output", body);
    }

    /* ── Request handlers ────────────────────────────────────────── */

    void handle_initialize(int req_seq) {
            dict caps = {
                "supportsConfigurationDoneRequest": true,
                "supportTerminateDebuggee": true
            };
            dict resp = {
                "seq": this->next_seq(),
                "type": "response",
                "request_seq": req_seq,
                "command": "initialize",
                "success": true
            };
            resp.body = caps;

            dap_send_dict(this.write_fp, resp);

            this->send_event_simple("initialized");
            this.state = 1;
        }

    void handle_launch(int req_seq, dict args) {
            if ("program" in args)
                this.program_path = (char *)args.program;

            dict resp = {
                "seq": this->next_seq(),
                "type": "response",
                "request_seq": req_seq,
                "command": "launch",
                "success": true
            };
            dap_send_dict(this.write_fp, resp);
            this.state = 3;
        }

    void handle_configuration_done(int req_seq) {
            dict resp = {
                "seq": this->next_seq(),
                "type": "response",
                "request_seq": req_seq,
                "command": "configurationDone",
                "success": true
            };
            dap_send_dict(this.write_fp, resp);
            this.state = 2;
        }

    void handle_threads(int req_seq) {
            /* Emit a real threads array via raw JSON (dict has no array type) */
            char json_buf[512];
            snprintf(json_buf, sizeof(json_buf),
                "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,\"command\":\"threads\",\"success\":true,\"body\":{\"threads\":[{\"id\":1,\"name\":\"main\"}]}}",
                this->next_seq(), req_seq);
            dap_send_message(this->write_fp, json_buf);
        }

        void handle_disconnect(int req_seq) {
            dict resp = {
                "seq": this->next_seq(),
                "type": "response",
                "request_seq": req_seq,
                "command": "disconnect",
                "success": true
            };
            dap_send_dict(this.write_fp, resp);
            this.state = 5;
            this.is_stopped = 0;
        }

        void handle_set_breakpoints(int req_seq) {
            FILE *ff = fopen("/tmp/classyc-dap-bps.txt","w");
            if (ff) fclose(ff);
            char json_buf[256];
            snprintf(json_buf, sizeof(json_buf),
                "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,\"command\":\"setBreakpoints\",\"success\":true,\"body\":{\"breakpoints\":[]}}",
                this->next_seq(), req_seq);
            dap_send_message(this->write_fp, json_buf);
        }

        void handle_set_breakpoints_raw(int req_seq, char *raw_json) {
            char src_path[1024]; src_path[0]='\0';
            /* crude parse: find "path":"..." */
            char *pp = strstr(raw_json, "\"path\"");
            if (pp) { char *colon = strchr(pp, ':'); if (colon) { char *q1=strchr(colon,'"'); if(q1){ char *q2=strchr(q1+1,'"'); if(q2){ int len=q2-(q1+1); if(len>0 && len<1023){ strncpy(src_path, q1+1, len); src_path[len]='\0'; } } } } }
            FILE *f = fopen("/tmp/classyc-dap-bps.txt","w");
            int count=0;
            int first_line=0;
            /* find all "line":<num> */
            char *p = raw_json;
            while ((p = strstr(p, "\"line\"")) != NULL) {
                p+=6;
                char *c=strchr(p, ':'); if(!c) break; p=c+1; while(*p==' '||*p=='\t') p++; int l=atoi(p); if(l>0){ if(f) fprintf(f,"%s:%d\n", src_path[0]?src_path:"?", l); if(count==0) first_line=l; count++; }
            }
            if (f) fclose(f);
            if (dap_logger_fd>=0) dprintf(dap_logger_fd, "setBreakpoints src=%s count=%d first=%d raw=%.200s\n", src_path, count, first_line, raw_json);
            char json_buf[4096];
            if (count>0) {
                /* report each verified breakpoint line (first is enough for many clients) */
                snprintf(json_buf, sizeof(json_buf),
                    "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,\"command\":\"setBreakpoints\",\"success\":true,\"body\":{\"breakpoints\":[{\"id\":1,\"verified\":true,\"line\":%d}]}}",
                    this->next_seq(), req_seq, first_line>0?first_line:2);
            } else {
                snprintf(json_buf, sizeof(json_buf),
                    "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,\"command\":\"setBreakpoints\",\"success\":true,\"body\":{\"breakpoints\":[]}}",
                    this->next_seq(), req_seq);
            }
            dap_send_message(this->write_fp, json_buf);
        }

        void handle_continue(int req_seq) {
            dict body = { "allThreadsContinued": true };
            dict resp = {
                "seq": this->next_seq(),
                "type": "response",
                "request_seq": req_seq,
                "command": "continue",
                "success": true
            };
            resp.body = body;
            dap_send_dict(this.write_fp, resp);
            this.state = 3;
            this.is_stopped = 0;
        }

        void handle_stack_trace(int req_seq) {
            char json_buf[2048];
            if (this.is_stopped && this.stop_line > 0) {
                char *sf = (char *)this.stop_file;
                if (!sf) sf = "";
                /* Escape only what we need — paths rarely contain quotes */
                snprintf(json_buf, sizeof(json_buf),
                    "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,\"command\":\"stackTrace\",\"success\":true,"
                    "\"body\":{\"stackFrames\":[{\"id\":1,\"name\":\"main\",\"line\":%d,\"column\":%d,"
                    "\"source\":{\"name\":\"%s\",\"path\":\"%s\"}}],\"totalFrames\":1}}",
                    this->next_seq(), req_seq, this.stop_line,
                    this.stop_col > 0 ? this.stop_col : 1,
                    sf, sf);
            } else {
                snprintf(json_buf, sizeof(json_buf),
                    "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,\"command\":\"stackTrace\",\"success\":true,"
                    "\"body\":{\"stackFrames\":[],\"totalFrames\":0}}",
                    this->next_seq(), req_seq);
            }
            dap_send_message(this->write_fp, json_buf);
        }

        /* Record stop location from a "__DAP_BRK__ file:line:col" child note */
        void record_stop_from_break_text(char *txt) {
            this.is_stopped = 1;
            this.stop_line = 0;
            this.stop_col = 0;
            this.stop_file = "";
            if (!txt) return;
            char *p = strstr(txt, "__DAP_BRK__");
            if (!p) return;
            p = p + 11; /* skip marker */
            while (*p == ' ' || *p == '\t') p++;
            char filebuf[1024];
            int i = 0;
            while (*p && *p != ':' && i < 1023) {
                filebuf[i] = *p;
                i = i + 1;
                p = p + 1;
            }
            filebuf[i] = '\0';
            if (*p == ':') {
                p = p + 1;
                this.stop_line = atoi(p);
                char *c2 = strchr(p, ':');
                if (c2) this.stop_col = atoi(c2 + 1);
            }
            if (filebuf[0]) this.stop_file = filebuf;
        }

    /* ── Dispatch ────────────────────────────────────────────────── */

    int dispatch_message(char *json_str) {
            dict msg = json(json_str);
            if (msg == 0) {
                fprintf(stderr, "[DAP] failed to parse message\n");
                return -1;
            }

            char *type    = (char *)msg.type;
            char *command = (char *)msg.command;
            int   req_seq = (int)(long)msg.seq;
            if (dap_logger_fd >= 0) {
                dprintf(dap_logger_fd, f"dispatch_message: type={type} command={command} seq={req_seq}\n");
            }

            if (this.verbose)
                printf("[DAP] <- %s\n", json_str);

            if (!type || strcmp(type, "request") != 0)
                return 0;

            if (!command) return 0;

            if (strcmp(command, "initialize") == 0) {
                this->handle_initialize(req_seq);
            } else if (strcmp(command, "launch") == 0) {
                dict args = msg.arguments;
                this->handle_launch(req_seq, args);
            } else if (strcmp(command, "configurationDone") == 0) {
                this->handle_configuration_done(req_seq);
            } else if (strcmp(command, "threads") == 0) {
                this->handle_threads(req_seq);
            } else if (strcmp(command, "disconnect") == 0) {
                this->handle_disconnect(req_seq);
                return 1;
            } else if (strcmp(command, "setBreakpoints") == 0) {
                this->handle_set_breakpoints_raw(req_seq, json_str);
            } else if (strcmp(command, "continue") == 0) {
                this->handle_continue(req_seq);
            } else if (strcmp(command, "stackTrace") == 0) {
                this->handle_stack_trace(req_seq);
            } else {
                /* Unknown command — send success anyway (be lenient) */
                dict resp = {
                    "seq": this->next_seq(),
                    "type": "response",
                    "request_seq": req_seq,
                    "command": command,
                    "success": true
                };
                dap_send_dict(this.write_fp, resp);
            }

            return 0;
        }

    /* ── Hooks for the jitrunner run loop ─────────────────────────── */

    void on_pre_run(char *bmir_path) {
        String msg = f"[jitrunner] running {bmir_path}\n";
        this->send_output("console", (char *)msg);
    }

    void on_post_run(int exit_code, int signal_num) {
        dict exit_body = { "exitCode": 0 };
        exit_body.exitCode = exit_code;
        this->send_event("exited", exit_body);

        if (signal_num) {
            String msg = f"program killed by signal {signal_num}\n";
            this->send_output("stderr", (char *)msg);
        } else {
            String msg = f"program exited with code {exit_code}\n";
            this->send_output("console", (char *)msg);
        }

        this->send_event_simple("terminated");
    }

    void on_file_changed(char *path) {
        String msg = f"[jitrunner] file changed: {path}, reloading...\n";
        this->send_output("console", (char *)msg);
    }
};

#endif /* DAP_H */
