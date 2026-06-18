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
  opts.keep_syms_p = 1;            /* keep symbol table for definition queries */
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

/* Encode a filesystem path into a file:// URI (percent-encoded).  Returns a
   freshly allocated string; caller frees. */
static char *path_to_uri (const char *path) {
  if (path == NULL) return NULL;
  size_t len = strlen(path);
  char *out = xmalloc(len * 3 + 8);
  char *o = out;
  *o++ = 'f', *o++ = 'i', *o++ = 'l', *o++ = 'e', *o++ = ':', *o++ = '/', *o++ = '/';
  for (const char *p = path; *p; p++) {
    if ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z') ||
        (p[0] >= '0' && p[0] <= '9') || p[0] == '-' || p[0] == '_' ||
        p[0] == '.' || p[0] == '~' || p[0] == '/') {
      *o++ = *p;
    } else {
      static const char hex[] = "0123456789ABCDEF";
      unsigned char c = *p;
      *o++ = '%'; *o++ = hex[c >> 4]; *o++ = hex[c & 0xF];
    }
  }
  *o = '\0';
  return out;
}

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

static int64_t obj_int (const DictValue *obj, const char *key) {
  DictValue *v = dict_object_get (obj, key);
  if (v != NULL && v->type == DICT_INT64) return v->int64_value;
  if (v != NULL && v->type == DICT_NUMBER) return (int64_t) v->number_value;
  return -1;
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

/* Keep the open document text(s) so we can answer definition requests.
   Simple fixed-size cache (keyed by URI). */
#define MAX_DOCS 16
static char *g_doc_uris[MAX_DOCS];
static char *g_doc_texts[MAX_DOCS];
static int g_num_docs = 0;

/* Convenience aliases for the most-recently seen document (used by some older paths) */
static char *g_doc_uri;
static char *g_doc_text;

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
static void store_document (const char *uri, const char *text) {
  if (uri == NULL) return;
  for (int i = 0; i < g_num_docs; i++) {
    if (g_doc_uris[i] && strcmp(g_doc_uris[i], uri) == 0) {
      free(g_doc_texts[i]);
      g_doc_texts[i] = text ? xstrdup(text) : NULL;
      g_doc_uri = g_doc_uris[i];
      g_doc_text = g_doc_texts[i];
      return;
    }
  }
  if (g_num_docs < MAX_DOCS) {
    g_doc_uris[g_num_docs] = xstrdup(uri);
    g_doc_texts[g_num_docs] = text ? xstrdup(text) : NULL;
    g_doc_uri = g_doc_uris[g_num_docs];
    g_doc_text = g_doc_texts[g_num_docs];
    g_num_docs++;
    return;
  }
  // cache full, overwrite the last slot
  free(g_doc_uris[g_num_docs-1]);
  free(g_doc_texts[g_num_docs-1]);
  g_doc_uris[g_num_docs-1] = xstrdup(uri);
  g_doc_texts[g_num_docs-1] = text ? xstrdup(text) : NULL;
  g_doc_uri = g_doc_uris[g_num_docs-1];
  g_doc_text = g_doc_texts[g_num_docs-1];
}

static void remove_document (const char *uri) {
  if (uri == NULL) return;
  for (int i = 0; i < g_num_docs; i++) {
    if (g_doc_uris[i] && strcmp(g_doc_uris[i], uri) == 0) {
      free(g_doc_uris[i]);
      free(g_doc_texts[i]);
      for (int j = i; j < g_num_docs - 1; j++) {
        g_doc_uris[j] = g_doc_uris[j+1];
        g_doc_texts[j] = g_doc_texts[j+1];
      }
      g_num_docs--;
      if (g_doc_uri && strcmp(g_doc_uri, uri) == 0) {
        g_doc_uri = (g_num_docs > 0 ? g_doc_uris[g_num_docs-1] : NULL);
        g_doc_text = (g_num_docs > 0 ? g_doc_texts[g_num_docs-1] : NULL);
      }
      return;
    }
  }
}

static void analyze_and_publish (const char *uri, const char *text) {
  store_document(uri, text);
  char *path = uri_to_path (uri);
  /* Keep symbols in the compiler context for subsequent definition queries. */
  /* We achieve this by setting keep_syms_p=1 in the opts used by analyze(). */
  analyze (path, text ? text : "");
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
  dict_object_set (caps, "definitionProvider", dict_create_bool (1));
  dict_object_set (caps, "referencesProvider", dict_create_bool (1));
  dict_object_set (info, "name", dict_create_string ("classyc-lsp"));
  dict_object_set (info, "version", dict_create_string ("0.1"));
  dict_object_set (result, "capabilities", caps);
  dict_object_set (result, "serverInfo", info);
  send_result (id, result);
  log_debug ("initialize: replied with capabilities");
}

/* Extract an identifier ( [A-Za-z_][A-Za-z0-9_]* ) around the 0-based character position
   on the given 0-based line. Returns 1 on success and fills out_name + [start0,end0).
   Simple ascii-only scanner — sufficient for ClassyC identifiers. */
static int extract_identifier_at (const char *text, int line0, int ch0,
                                  char *out_name, size_t name_cap,
                                  int *start0, int *end0) {
  if (text == NULL || out_name == NULL || name_cap == 0) return 0;
  const char *p = text;
  int l = 0;
  while (*p && l < line0) { if (*p++ == '\n') l++; }
  if (l != line0) return 0;
  const char *line_start = p;
  /* advance to the column */
  while (*p && *p != '\n' && (p - line_start) < ch0) p++;
  /* check we are on an identifier char */
  if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_' ||
        (*p >= '0' && *p <= '9'))) {
    /* allow scanning into a word if the cursor is on the boundary after the word */
    if (ch0 > 0) {
      const char *left = p - 1;
      if (left > line_start && ((*left >= 'A'&&*left<='Z')||(*left>='a'&&*left<='z')||*left=='_' )) {
        p = left;
      } else return 0;
    } else return 0;
  }
  /* scan left to word start */
  const char *q = p;
  while (q > line_start &&
         ((* (q-1) >= 'A' && *(q-1) <= 'Z') || (*(q-1) >= 'a' && *(q-1) <= 'z') ||
          *(q-1) == '_' || (*(q-1) >= '0' && *(q-1) <= '9'))) q--;
  /* scan right */
  const char *r = p;
  while (*r && ((*r >= 'A' && *r <= 'Z') || (*r >= 'a' && *r <= 'z') || *r == '_' ||
                (*r >= '0' && *r <= '9'))) r++;
  	size_t len = r - q;
  	if (len == 0 || len >= name_cap) return 0;
  	memcpy(out_name, q, len);
  	out_name[len] = '\0';
  	if (start0) *start0 = (int)(q - line_start);
  	if (end0) *end0 = (int)(r - line_start);
  	return 1;
  }

  /* Detect obj.member or obj->member at/around the cursor (supports simple chaining and call parens).
     Fills out_receiver (the left base identifier, e.g. "snatch" even for snatch.find().trim)
     and out_member (the identifier under cursor).
     Returns 1 on success (member access seen; receiver may be "" if complex expr before dot).
     Returns 0 if no preceding dot/arrow before the identifier (bare name). */
  static int extract_member_access (const char *text, int line0, int ch0,
                                    char *out_receiver, size_t rec_cap,
                                    char *out_member, size_t mem_cap) {
  	if (text == NULL || out_receiver == NULL || rec_cap == 0 ||
  	    out_member == NULL || mem_cap == 0) return 0;
  	char member[256];
  	int mstart = -1, mend = -1;
  	if (!extract_identifier_at (text, line0, ch0, member, sizeof member, &mstart, &mend))
  	  return 0;
  	/* locate the line */
  	const char *p = text;
  	int l = 0;
  	while (*p && l < line0) { if (*p++ == '\n') l++; }
  	if (l != line0) return 0;
  	const char *line = p;
  	/* look immediately left of the member for . or -> (after optional ws) */
  	int i = mstart - 1;
  	while (i >= 0 && isspace ((unsigned char)line[i])) --i;
  	if (i < 0) return 0;
  	if (line[i] != '.' && !(line[i] == '>' && i > 0 && line[i-1] == '-'))
  	  return 0;  /* bare identifier */
  	/* copy the ident under cursor as the member name */
  	strncpy (out_member, member, mem_cap - 1);
  	out_member[mem_cap - 1] = '\0';
  	/* walk left from before the dot/arrow to find base receiver ident */
  	int pos = (line[i] == '.' ? i - 1 : i - 2);
  	for (;;) {
  	  while (pos >= 0 && isspace ((unsigned char)line[pos])) --pos;
  	  if (pos < 0) break;
  	  char ch = line[pos];
  	  if (ch == ')') {
  	    int depth = 1; --pos;
  	    while (pos >= 0 && depth > 0) {
  	      if (line[pos] == ')') ++depth;
  	      else if (line[pos] == '(') --depth;
  	      --pos;
  	    }
  	    continue;
  	  }
  	  if (ch == ']') {
  	    int depth = 1; --pos;
  	    while (pos >= 0 && depth > 0) {
  	      if (line[pos] == ']') ++depth;
  	      else if (line[pos] == '[') --depth;
  	      --pos;
  	    }
  	    continue;
  	  }
  	  if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' ||
  	        (ch >= '0' && ch <= '9'))) {
  	    break;
  	  }
  	  /* collect whole preceding identifier */
  	  int id_end = pos + 1;
  	  while (pos >= 0) {
  	    ch = line[pos];
  	    if (!((ch >= 'a'&&ch<='z')||(ch>='A'&&ch<='Z')||ch=='_'||(ch>='0'&&ch<='9'))) break;
  	    --pos;
  	  }
  	  int id_start = pos + 1;
  	  size_t len = (id_end > id_start) ? (id_end - id_start) : 0;
  	  if (len == 0) break;
  	  /* is this id preceded by a dot? (chained expr) */
  	  int k = id_start - 1;
  	  while (k >= 0 && isspace ((unsigned char)line[k])) --k;
  	  int chained = 0;
  	  if (k >= 0) {
  	    if (line[k] == '.') chained = 1;
  	    else if (k > 0 && line[k] == '>' && line[k-1] == '-') chained = 1;
  	  }
  	  if (chained) {
  	    /* skip this intermediate id (a method in chain) and its preceding dot; continue left */
  	    pos = k - 1;
  	    continue;
  	  }
  	  /* root receiver name */
  	  if (len >= rec_cap) return 0;
  	  memcpy (out_receiver, &line[id_start], len);
  	  out_receiver[len] = '\0';
  	  return 1;
  	}
  	/* dot seen but no simple root receiver ident (complex expr or literal etc) */
  	out_receiver[0] = '\0';
  	return 1;
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

static void handle_definition (DictValue *id, DictValue *params) {
  if (id == NULL) {
    log_debug("handle_definition: no id (ignored)");
    return;
  }
  log_debug("handle_definition: entered (id present)");

  DictValue *doc = dict_object_get (params, "textDocument");
  const char *uri = doc != NULL ? obj_str (doc, "uri") : NULL;
  log_debug("handle_definition: request uri=%s", uri ? uri : "<null>");

  char *doc_text = NULL;
  for (int i = 0; i < g_num_docs; i++) {
    if (g_doc_uris[i] && strcmp(g_doc_uris[i], uri) == 0) {
      doc_text = g_doc_texts[i];
      break;
    }
  }

  if (doc_text == NULL) {
    log_debug("handle_definition: no cached text for uri=%s -> returning null", uri ? uri : "<null>");
    send_result (id, dict_create_null ());
    return;
  }
  log_debug("handle_definition: found cached text for uri (len=%zu)", strlen(doc_text));

  	DictValue *pos = dict_object_get (params, "position");
  	int line = (int) obj_int (pos, "line");
  	int ch   = (int) obj_int (pos, "character");
  	log_debug("handle_definition: got request line=%d ch=%d (0-based) uri=%s", line, ch, uri);
  	if (line < 0 || ch < 0) {
  	  log_debug("handle_definition: invalid position");
  	  send_result (id, dict_create_null ());
  	  return;
  	}

  	/* Try to detect a receiver.member (or ->) access first. Supports chained calls. */
  	char receiver[256] = {0};
  	char member[256]   = {0};
  	int has_member = extract_member_access (doc_text, line, ch, receiver, sizeof receiver, member, sizeof member);
  	char name[256];
  	int start0 = -1, end0 = -1;
  	if (has_member) {
  	  log_debug("handle_definition: member access: receiver='%s' member='%s'", receiver, member);
  	  if (!extract_identifier_at (doc_text, line, ch, name, sizeof name, &start0, &end0))
  	    strncpy (name, member, sizeof name - 1);
  	} else {
  	  if (!extract_identifier_at (doc_text, line, ch, name, sizeof name, &start0, &end0)) {
  	    log_debug("handle_definition: no identifier found at line=%d ch=%d", line, ch);
  	    send_result (id, dict_create_null ());
  	    return;
  	  }
  	  log_debug("handle_definition: extracted identifier '%s' (line=%d ch0=%d..%d)", name, line, start0, end0);
  	}

  	/* Ensure the compiler has up-to-date kept symbols for this doc */
  	log_debug("handle_definition: forcing re-analyze for kept symbols (text len=%zu)", strlen(doc_text));
  	{
  	  char *path = uri_to_path (uri);
  	  analyze (path, doc_text);
  	  free (path);
  	}

  	c2mir_pos_t loc;
  	int found = 0;
  	if (has_member && receiver[0] != '\0') {
  	  found = c2mir_find_member_definition (g_ctx, receiver, member, &loc);
  	  if (found)
  	    log_debug("handle_definition: c2mir_find_member_definition returned 1 for %s.%s", receiver, member);
  	}
  	if (!found) {
  	  /* plain name (top-level, or fallback for members, or bare name case) */
  	  found = c2mir_find_definition (g_ctx, name, &loc);
  	  log_debug("handle_definition: c2mir_find_definition returned %d for '%s'", found, name);
  	}

  	if (!found) {
  	  log_debug("handle_definition: symbol '%s' NOT FOUND", name);
  	  send_result (id, dict_create_null ());
  	  return;
  	}
  	log_debug("handle_definition: symbol '%s' FOUND at %s:%d:%d (1-based)",
  	          name, loc.fname ? loc.fname : "<null>", loc.lno, loc.ln_pos);
  /* Build LSP Location. Compiler loc is 1-based; LSP range is 0-based. */
  DictValue *loc_obj = dict_create_object ();
  DictValue *range = dict_create_object ();
  DictValue *start = dict_create_object ();
  DictValue *end = dict_create_object ();
  int dline = loc.lno > 0 ? loc.lno - 1 : 0;
  int dcol  = loc.ln_pos > 0 ? loc.ln_pos - 1 : 0;
  int name_len = (int) strlen (name);
  if (name_len < 1) name_len = 1;
  dict_object_set (start, "line", dict_create_int64 (dline));
  dict_object_set (start, "character", dict_create_int64 (dcol));
  dict_object_set (end, "line", dict_create_int64 (dline));
  dict_object_set (end, "character", dict_create_int64 (dcol + name_len));
  dict_object_set (range, "start", start);
  dict_object_set (range, "end", end);
  dict_object_set (loc_obj, "uri", dict_create_string (uri));
  dict_object_set (loc_obj, "range", range);
  send_result (id, loc_obj);
}

static void handle_references (DictValue *id, DictValue *params) {
  if (id == NULL) {
    log_debug("handle_references: no id (ignored)");
    return;
  }
  log_debug("handle_references: entered");

  DictValue *doc = dict_object_get (params, "textDocument");
  const char *uri = doc != NULL ? obj_str (doc, "uri") : NULL;

  char *doc_text = NULL;
  for (int i = 0; i < g_num_docs; i++) {
    if (g_doc_uris[i] && strcmp(g_doc_uris[i], uri) == 0) {
      doc_text = g_doc_texts[i];
      break;
    }
  }

  if (doc_text == NULL) {
    log_debug("handle_references: no cached text for uri=%s -> returning null", uri ? uri : "<null>");
    send_result (id, dict_create_null ());
    return;
  }

  DictValue *pos = dict_object_get (params, "position");
  int line = (int) obj_int (pos, "line");
  int ch   = (int) obj_int (pos, "character");
  if (line < 0 || ch < 0) {
    send_result (id, dict_create_null ());
    return;
  }

  char name[256];
  if (!extract_identifier_at (doc_text, line, ch, name, sizeof name, NULL, NULL)) {
    log_debug("handle_references: no identifier at line=%d ch=%d", line, ch);
    send_result (id, dict_create_null ());
    return;
  }
  log_debug("handle_references: identifier '%s'", name);

  /* Ensure compiler has up-to-date kept symbols */
  {
    char *path = uri_to_path (uri);
    analyze (path, doc_text);
    free (path);
  }

  c2mir_pos_t *refs = NULL;
  int count = c2mir_find_references (g_ctx, name, &refs);
  if (count <= 0 || refs == NULL) {
    log_debug("handle_references: no references found for '%s'", name);
    send_result (id, dict_create_null ());
    return;
  }

  /* Build an LSP Location array */
  DictValue *loc_array = dict_create_array ();
  for (int i = 0; i < count; i++) {
    DictValue *loc_obj = dict_create_object ();
    DictValue *range = dict_create_object ();
    DictValue *start = dict_create_object ();
    DictValue *end = dict_create_object ();
    int dline = refs[i].lno > 0 ? refs[i].lno - 1 : 0;
    int dcol  = refs[i].ln_pos > 0 ? refs[i].ln_pos - 1 : 0;
    int name_len = (int) strlen (name);
    if (name_len < 1) name_len = 1;

    dict_object_set (start, "line", dict_create_int64 (dline));
    dict_object_set (start, "character", dict_create_int64 (dcol));
    dict_object_set (end, "line", dict_create_int64 (dline));
    dict_object_set (end, "character", dict_create_int64 (dcol + name_len));
    dict_object_set (range, "start", start);
    dict_object_set (range, "end", end);

    char *ref_uri = path_to_uri (refs[i].fname);
    if (ref_uri != NULL) {
      dict_object_set (loc_obj, "uri", dict_create_string (ref_uri));
      free (ref_uri);
    } else {
      dict_object_set (loc_obj, "uri", dict_create_string (uri));
    }
    dict_object_set (loc_obj, "range", range);
    dict_array_append (loc_array, loc_obj);
  }

  c2mir_free_references (refs);
  send_result (id, loc_array);
  log_debug("handle_references: sent %d location(s) for '%s'", count, name);
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

  log_debug("handle_message: about to dispatch method=%s (params type=%d, id=%p)", 
            method, (int)(params ? params->type : -1), (void*)id);

  int handled = 0;

  if (strcmp (method, "initialize") == 0) {
    handle_initialize (id);
    handled = 1;
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
      remove_document (uri);
      c2mir_release_analysis (g_ctx);
    }
  } else if (strcmp (method, "textDocument/definition") == 0) {
    log_debug("handle_message: dispatching textDocument/definition (will call handle_definition)");
    handle_definition (id, params);
  } else if (strcmp (method, "textDocument/references") == 0) {
    log_debug("handle_message: dispatching textDocument/references");
    handle_references (id, params);
  } else {
    if (id != NULL) {
      log_debug("handle_message: unhandled request method=%s", method ? method : "(null)");
    }
  }
  /* Unknown requests (with an id) are ignored; a fuller server would reply with
     a MethodNotFound error, but ignoring is safe for diagnostics-only use. */
}

int main (void) {
  const char *log_path = getenv ("CLASSYC_LSP_LOG");
  if (log_path == NULL) log_path = "/tmp/classyc-lsp.log";
  log_debug_open (log_path);
  log_debug ("=== classyc-lsp started (pid %ld) === (BUILD WITH DEFINITION DEBUG LOGS)", (long) getpid ());

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
