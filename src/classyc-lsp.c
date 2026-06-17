/* classyc-lsp.c — a minimal Language Server for ClassyC (.cy) files.

   A second "main" alongside classyc-driver.c that drives the SAME compiler front
   end (preprocess -> parse -> context check) but stops before code generation
   (options.no_gen_p), so editors get accurate ClassyC diagnostics — class /
   String / dict / lambda / generics extensions that clangd flags as errors.

   Transport: Language Server Protocol over stdin/stdout (Content-Length framed,
   JSON-RPC 2.0).  JSON is built/parsed with the bundled single-header dict.h.

   Diagnostics flow: the compiler's error()/warning() are routed through logger.h
   to a registered sink (log_set_diag_sink).  The sink runs synchronously inside
   the compile call, so it copies file/message immediately — the compiler frees
   the raw source as it parses, so borrowed pointers must not be kept.

   Disambiguation: clangd already claims .c/.cpp, so wire this server to a
   ClassyC-specific extension (.cy) in your editor (see the doc comment at EOF).

   Copyright (C) 2025-2026 ClassyC project.  MIT-style, same as the project.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>

#include "mir.h"
#include "classyc.h"
#include "logger.h" /* log_set_diag_sink, log_diag_t — defined in classyc.c TU */

#define C2M_DICT_API static
#include "dict.h" /* single-header JSON (build + parse) */

/* ───────────────────────────── small utilities ──────────────────────────── */

static void *xmalloc (size_t n) {
  void *p = malloc (n);
  if (p == NULL) {
    fprintf (stderr, "classyc-lsp: out of memory\n");
    exit (1);
  }
  return p;
}

static char *xstrdup (const char *s) {
  size_t n = strlen (s) + 1;
  char *p = xmalloc (n);
  memcpy (p, s, n);
  return p;
}

/* ─────────────────────────── diagnostic collection ──────────────────────── */

typedef struct {
  int line, col;  /* 1-based, as produced by the compiler */
  int error_p;    /* 1 = error, 0 = warning */
  char *message;  /* owned copy */
} diag_t;

static diag_t *g_diags;
static size_t g_ndiags, g_capdiags;
static const char *g_cur_file; /* source name of the document being analyzed */

static void diags_reset (void) {
  for (size_t i = 0; i < g_ndiags; i++) free (g_diags[i].message);
  g_ndiags = 0;
}

/* logger.h sink: called once per compiler error/warning, synchronously, while
   d->file and d->message are still valid.  We copy what we keep. */
static void diag_sink (void *data, const log_diag_t *d) {
  (void) data;
  /* Only surface diagnostics for the document itself (skip those originating in
     the embedded standard headers, e.g. "<environment>"). */
  if (d->file == NULL || g_cur_file == NULL || strcmp (d->file, g_cur_file) != 0) return;
  if (d->message == NULL) return;

  if (g_ndiags == g_capdiags) {
    g_capdiags = g_capdiags ? g_capdiags * 2 : 16;
    g_diags = realloc (g_diags, g_capdiags * sizeof (diag_t));
    if (g_diags == NULL) {
      fprintf (stderr, "classyc-lsp: out of memory\n");
      exit (1);
    }
  }
  g_diags[g_ndiags].line = d->line;
  g_diags[g_ndiags].col = d->col;
  g_diags[g_ndiags].error_p = d->error_p;
  g_diags[g_ndiags].message = xstrdup (d->message);
  g_ndiags++;
}

/* ───────────────────────────── compiler driver ──────────────────────────── */

typedef struct {
  const char *code;
  size_t len, pos;
} srcbuf_t;

static int src_getc (void *data) {
  srcbuf_t *s = data;
  return s->pos >= s->len ? EOF : (unsigned char) s->code[s->pos++];
}

static MIR_context_t g_ctx;      /* reused across analyses */
static size_t g_module_num;

/* Run preprocess + parse + context check (no codegen) on `text`, populating
   g_diags with the diagnostics that belong to `path`. */
static void analyze (const char *path, const char *text) {
  struct c2mir_options opts;
  srcbuf_t sb;

  diags_reset ();
  g_cur_file = path;

  memset (&opts, 0, sizeof opts);
  opts.message_file = NULL;        /* no textual diagnostics — sink only */
  opts.no_gen_p = 1;               /* analyze only: stop after the checker */
  opts.module_num = g_module_num++;

  sb.code = text;
  sb.len = strlen (text);
  sb.pos = 0;

  log_set_diag_sink (diag_sink, NULL);
  c2mir_compile (g_ctx, &opts, src_getc, &sb, path, NULL);
  log_set_diag_sink (NULL, NULL);
  g_cur_file = NULL;
  log_debug ("analyze %s: %zu diagnostic(s)", path, g_ndiags);
}

/* ───────────────────────────────── URIs ─────────────────────────────────── */

/* Decode a file:// URI into a filesystem path (percent-decoded).  Returns a
   freshly allocated string; caller frees. */
static char *uri_to_path (const char *uri) {
  const char *p = uri;
  if (strncmp (p, "file://", 7) == 0) {
    p += 7;
    /* file://host/path keeps the leading '/'; an empty host gives file:///path */
    const char *slash = strchr (p, '/');
    if (slash != NULL) p = slash; /* drop any authority component */
  }
  char *out = xmalloc (strlen (p) + 1);
  char *o = out;
  while (*p != '\0') {
    if (p[0] == '%' && isxdigit ((unsigned char) p[1]) && isxdigit ((unsigned char) p[2])) {
      int hi = p[1] <= '9' ? p[1] - '0' : (tolower (p[1]) - 'a' + 10);
      int lo = p[2] <= '9' ? p[2] - '0' : (tolower (p[2]) - 'a' + 10);
      *o++ = (char) ((hi << 4) | lo);
      p += 3;
    } else {
      *o++ = *p++;
    }
  }
  *o = '\0';
  return out;
}

/* ───────────────────────────── JSON helpers ─────────────────────────────── */

/* Serialize `v` to a freshly allocated JSON string (growing the buffer until it
   fits).  Returns NULL on failure.  Caller frees. */
static char *json_to_string (const DictValue *v) {
  size_t cap = 4096;
  for (;;) {
    char *buf = malloc (cap);
    if (buf == NULL) return NULL;
    if (dict_serialize_json (v, buf, cap, 0) != NULL) return buf;
    free (buf);
    if (cap > (size_t) 64 * 1024 * 1024) return NULL; /* runaway guard */
    cap *= 2;
  }
}

/* Borrowed string field of a JSON object (or NULL). */
static const char *obj_str (const DictValue *obj, const char *key) {
  DictValue *v = dict_object_get (obj, key);
  return (v != NULL && v->type == DICT_STRING) ? v->string_value : NULL;
}

/* Deep-copy a JSON-RPC id (integer or string) so it can be echoed in a response;
   returns DICT_NULL when there is no usable id. */
static DictValue *clone_id (DictValue *id) {
  if (id == NULL) return dict_create_null ();
  switch (id->type) {
  case DICT_INT64: return dict_create_int64 (id->int64_value);
  case DICT_NUMBER: return dict_create_int64 ((int64_t) id->number_value);
  case DICT_STRING: return dict_create_string (id->string_value);
  default: return dict_create_null ();
  }
}

/* ───────────────────────────── transport ────────────────────────────────── */

static void write_message (const char *body) {
  size_t len = strlen (body);
  printf ("Content-Length: %zu\r\n\r\n", len);
  fwrite (body, 1, len, stdout);
  fflush (stdout);
  log_debug (">> sent %zu bytes", len);
}

static void send_value (DictValue *v) {
  char *s = json_to_string (v);
  if (s != NULL) {
    write_message (s);
    free (s);
  }
  dict_destroy (v);
}

/* Read one LSP message body (the JSON payload after the headers).
   Returns a malloc'd NUL-terminated buffer.  On return *out_len holds the
   exact number of bytes placed in the buffer (i.e. the Content-Length value or
   less on a short read).  Returns NULL at EOF. */
static char *read_message (size_t *out_len) {
  size_t content_length = 0;
  char line[8192];
  int c, n;

  for (;;) { /* header lines, terminated by a blank line */
    n = 0;
    while ((c = getchar ()) != EOF && c != '\n') {
      if (n < (int) sizeof (line) - 1) line[n++] = (char) c;
    }
    if (c == EOF && n == 0) return NULL;
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\r') line[n - 1] = '\0';
    if (line[0] == '\0') break; /* blank line: end of headers */
    if (strncmp (line, "Content-Length:", 15) == 0)
      content_length = strtoul (line + 15, NULL, 10);
    if (c == EOF) return NULL;
  }
  char *body = xmalloc (content_length + 1);
  size_t got = 0;
  while (got < content_length) {
    size_t r = fread (body + got, 1, content_length - got, stdin);
    if (r == 0) break;
    got += r;
  }
  body[got] = '\0';
  if (out_len != NULL) *out_len = got;
  log_debug ("<< Content-Length: %zu , fread got: %zu", content_length, got);
  if (got < content_length)
    log_debug ("<< short read: got %zu of %zu bytes (stdin closed?)", got, content_length);
  else
    log_debug ("<< read %zu bytes", got);
  return body;
}

/* ─────────────────────────── LSP message handlers ───────────────────────── */

static int g_shutdown_requested;

/* Build the publishDiagnostics params for `uri` from the current g_diags. */
static DictValue *build_diagnostics_params (const char *uri) {
  DictValue *params = dict_create_object ();
  DictValue *arr = dict_create_array ();

  dict_object_set (params, "uri", dict_create_string (uri));
  for (size_t i = 0; i < g_ndiags; i++) {
    int line = g_diags[i].line > 0 ? g_diags[i].line - 1 : 0; /* LSP is 0-based */
    int ch = g_diags[i].col > 0 ? g_diags[i].col - 1 : 0;
    DictValue *d = dict_create_object ();
    DictValue *range = dict_create_object ();
    DictValue *start = dict_create_object ();
    DictValue *end = dict_create_object ();

    dict_object_set (start, "line", dict_create_int64 (line));
    dict_object_set (start, "character", dict_create_int64 (ch));
    dict_object_set (end, "line", dict_create_int64 (line));
    dict_object_set (end, "character", dict_create_int64 (ch + 1));
    dict_object_set (range, "start", start);
    dict_object_set (range, "end", end);
    dict_object_set (d, "range", range);
    dict_object_set (d, "severity", dict_create_int64 (g_diags[i].error_p ? 1 : 2));
    dict_object_set (d, "source", dict_create_string ("classyc"));
    dict_object_set (d, "message", dict_create_string (g_diags[i].message));
    dict_array_append (arr, d);
  }
  dict_object_set (params, "diagnostics", arr);
  return params;
}

static void publish_diagnostics (const char *uri) {
  DictValue *msg = dict_create_object ();
  dict_object_set (msg, "jsonrpc", dict_create_string ("2.0"));
  dict_object_set (msg, "method", dict_create_string ("textDocument/publishDiagnostics"));
  dict_object_set (msg, "params", build_diagnostics_params (uri));
  send_value (msg);
}

/* Analyze the document text and immediately publish its diagnostics. */
static void analyze_and_publish (const char *uri, const char *text) {
  char *path = uri_to_path (uri);
  analyze (path, text != NULL ? text : "");
  free (path);
  publish_diagnostics (uri);
}

static void send_result (DictValue *id, DictValue *result) {
  DictValue *msg = dict_create_object ();
  dict_object_set (msg, "jsonrpc", dict_create_string ("2.0"));
  dict_object_set (msg, "id", clone_id (id));
  dict_object_set (msg, "result", result);
  send_value (msg);
}

static void handle_initialize (DictValue *id) {
  DictValue *result = dict_create_object ();
  DictValue *caps = dict_create_object ();
  DictValue *info = dict_create_object ();

  /* textDocumentSync = 1 (full): the client sends the whole document on change. */
  dict_object_set (caps, "textDocumentSync", dict_create_int64 (1));
  dict_object_set (info, "name", dict_create_string ("classyc-lsp"));
  dict_object_set (info, "version", dict_create_string ("0.1"));
  dict_object_set (result, "capabilities", caps);
  dict_object_set (result, "serverInfo", info);
  send_result (id, result);
  log_debug ("initialize: replied with capabilities");
}

/* didChange (full sync): params.contentChanges is an array; the last element's
   "text" is the new full document. */
static const char *last_change_text (DictValue *params) {
  DictValue *changes = dict_object_get (params, "contentChanges");
  if (changes == NULL || changes->type != DICT_ARRAY || changes->array_value.length == 0)
    return NULL;
  DictValue *last = changes->array_value.items[changes->array_value.length - 1];
  if (last == NULL || last->type != DICT_OBJECT) return NULL;
  return obj_str (last, "text");
}

static void handle_message (DictValue *root) {
  const char *method = obj_str (root, "method");
  DictValue *id = dict_object_get (root, "id");
  DictValue *params = dict_object_get (root, "params");

  if (method == NULL) {
    log_debug ("recv message with no method (ignored)");
    return;
  }
  log_debug ("recv method=%s%s", method, id != NULL ? " (request)" : " (notification)");

  if (strcmp (method, "initialize") == 0) {
    handle_initialize (id);
  } else if (strcmp (method, "shutdown") == 0) {
    g_shutdown_requested = 1;
    send_result (id, dict_create_null ());
  } else if (strcmp (method, "exit") == 0) {
    exit (g_shutdown_requested ? 0 : 1);
  } else if (strcmp (method, "initialized") == 0) {
    /* notification: nothing to do */
  } else if (params == NULL || params->type != DICT_OBJECT) {
    /* the remaining methods all need params */
  } else if (strcmp (method, "textDocument/didOpen") == 0) {
    DictValue *doc = dict_object_get (params, "textDocument");
    if (doc != NULL && doc->type == DICT_OBJECT)
      analyze_and_publish (obj_str (doc, "uri"), obj_str (doc, "text"));
  } else if (strcmp (method, "textDocument/didChange") == 0) {
    DictValue *doc = dict_object_get (params, "textDocument");
    const char *uri = doc != NULL ? obj_str (doc, "uri") : NULL;
    if (uri != NULL) analyze_and_publish (uri, last_change_text (params));
  } else if (strcmp (method, "textDocument/didSave") == 0) {
    DictValue *doc = dict_object_get (params, "textDocument");
    const char *uri = doc != NULL ? obj_str (doc, "uri") : NULL;
    const char *text = obj_str (params, "text"); /* present only if includeText */
    if (uri != NULL && text != NULL) analyze_and_publish (uri, text);
  } else if (strcmp (method, "textDocument/didClose") == 0) {
    DictValue *doc = dict_object_get (params, "textDocument");
    const char *uri = doc != NULL ? obj_str (doc, "uri") : NULL;
    if (uri != NULL) { /* clear diagnostics for the closed document */
      diags_reset ();
      publish_diagnostics (uri);
    }
  }
  /* Unknown requests (with an id) are ignored; a fuller server would reply with
     a MethodNotFound error, but ignoring is safe for diagnostics-only use. */
}

int main (void) {
  const char *log_path = getenv ("CLASSYC_LSP_LOG");
  if (log_path == NULL) log_path = "/tmp/classyc-lsp.log";
  log_debug_open (log_path);
  log_debug ("=== classyc-lsp started (pid %ld) ===", (long) getpid ());

  g_ctx = MIR_init ();
  c2mir_init (g_ctx);

  for (;;) {
    size_t body_len = 0;
    char *body = read_message (&body_len);
    if (body == NULL) {
      log_debug ("stdin closed (EOF); leaving read loop");
      break;
    }
    char errbuf[256];
    errbuf[0] = '\0';
    /* Pass the exact received frame length (body_len), not strlen(body).
       This makes parse errors accurate and eliminates any possibility of
       strlen stopping at an embedded NUL or counting extra garbage. */
    DictValue *root = dict_deserialize_json_len (body, body_len, errbuf, sizeof errbuf);
    if (root == NULL) {
      log_debug ("JSON parse failed (%s) for %zu-byte body:",
                 errbuf[0] != '\0' ? errbuf : "no detail", body_len);
      log_debug ("  body: [%s]", body);
      /* Dump exact body bytes to /tmp/failed_json.bin for isolated dict.h testing */
      {
        FILE *dbg = fopen ("/tmp/failed_json.bin", "wb");
        if (dbg) {
          fwrite (body, 1, body_len, dbg);
          fclose (dbg);
        }
      }
    }
    free (body);
    if (root != NULL) {
      if (root->type == DICT_OBJECT) handle_message (root);
      dict_destroy (root);
    }
  }
  log_debug ("=== classyc-lsp exiting ===");

  diags_reset ();
  free (g_diags);
  c2mir_finish (g_ctx);
  MIR_finish (g_ctx);
  return 0;
}

/* ── Editor setup ─────────────────────────────────────────────────────────
   clangd claims C/C++ (.c/.cpp/.h), so register ClassyC under its own language
   id + extension (.cy) and point that language at this server.

   Zed (settings.json):
     "languages": { "ClassyC": { "language_servers": ["classyc-lsp"] } },
     "lsp": { "classyc-lsp": { "binary": { "path": ".../bin/classyc-lsp" } } }
   plus a small extension mapping the ".cy" suffix to the "ClassyC" language.

   VS Code: contribute a language `classyc` for `.cy` files and start this
   binary as its server (stdio transport) from a thin extension.  */
