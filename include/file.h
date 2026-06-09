/* =========================================================================
   inc/file.h — File class for the c2mir extended-C compiler
   =========================================================================

   Design: Go / Python / C# -inspired convenience with RAII via defer/delete.

   ── Typical patterns ──────────────────────────────────────────────────────

   // Scope-guarded open; fclose runs automatically when the block exits
   File *f = File.open("data.json", "r");
   defer delete f;
   if (!f->ok()) { ... handle error ... }
   char *text = f->read_all();   // malloc'd, caller frees
   free(text);

   // One-liner static helpers (open + operate + close internally)
   char *src  = File.read_text("config.json");  // returns NULL on error
   int   ok   = File.write_text("out.txt", src);
   int   here = File.exists("out.txt");

   // Streaming: iterate every line, stop early by returning 0 from callback
   f->each_line(my_cb, &my_ctx);

   ── Callback signature ────────────────────────────────────────────────────

     int my_cb(char *line_or_chunk, int len_or_lineno, void *ctx);
     Return 0 to stop early, non-zero to continue.

     each_line  → arg 2 is the 1-based line number
     each_chunk → arg 2 is the number of bytes in this chunk

   ── Notes ─────────────────────────────────────────────────────────────────

   * read_all / read_line return malloc'd buffers — the caller must free().
   * each_line / each_chunk rewind to byte 0 before iterating and pass the
     caller a temporary buffer valid only for the duration of the callback.
   * The destructor calls fclose; a File opened but never explicitly closed
     is cleaned up by `delete f` or `defer delete f` just like any other
     heap object.
   ========================================================================= */

#ifndef FILE_H
#define FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class File {
    FILE *handle;   /* underlying C stream; NULL when closed or on error */
    int   error;    /* 0 = ok, 1 = failed to open                        */

    /* ── Private constructor — use File.open() ─────────────────────── */
    File(FILE *fp, int err) {
        this->handle = fp;
        this->error  = err;
    }

    /* ── Destructor: runs on `delete f` or `defer delete f` ────────── */
    ~File() {
        if (this->handle) {
            fclose(this->handle);
            this->handle = 0;
        }
    }

    /* ================================================================
       Static factory and convenience methods
       ================================================================ */

    /* Open a file.  mode follows fopen conventions: "r", "w", "a",
       "rb", "wb", "r+", …  Returns a heap-allocated File* that the
       caller should free with `delete` (or `defer delete`). */
    static File* open(char *path, char *mode) {
        FILE *fp = fopen(path, mode);
        return new File(fp, fp ? 0 : 1);
    }

    /* Read the entire file into a malloc'd C string.  The caller must
       free() the result.  Returns NULL on error. */
    static char* read_text(char *path) {
        FILE *fp = fopen(path, "r");
        if (!fp) return 0;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz < 0) { fclose(fp); return 0; }
        char *buf = (char *)malloc(sz + 1);
        if (!buf) { fclose(fp); return 0; }
        long got = (long)fread(buf, 1, sz, fp);
        buf[got] = '\0';
        fclose(fp);
        return buf;
    }

    /* Write a C string to a file (creates or overwrites).
       Returns 0 on success, -1 on error. */
    static int write_text(char *path, char *data) {
        FILE *fp = fopen(path, "w");
        if (!fp) return -1;
        int r = fputs(data, fp);
        fclose(fp);
        return (r < 0) ? -1 : 0;
    }

    /* Append a C string to a file (creates if not present).
       Returns 0 on success, -1 on error. */
    static int append_text(char *path, char *data) {
        FILE *fp = fopen(path, "a");
        if (!fp) return -1;
        int r = fputs(data, fp);
        fclose(fp);
        return (r < 0) ? -1 : 0;
    }

    /* Return 1 if path names an existing, readable file; 0 otherwise. */
    static int exists(char *path) {
        FILE *fp = fopen(path, "r");
        if (!fp) return 0;
        fclose(fp);
        return 1;
    }

    /* Delete a file.  Returns 0 on success, non-zero on failure.
       (Delegates to the C standard-library remove() function.) */
    static int remove_file(char *path) {
        return remove(path);
    }

    /* ================================================================
       Instance status
       ================================================================ */

    /* 1 when the file was opened successfully and is still open. */
    int ok() {
        return this->handle != 0 && this->error == 0;
    }

    /* 1 when positioned at (or past) end of file, or when not open.
       Uses position comparison in addition to feof() so that seeking to
       the end also reports eof (matching Python/C# behaviour). */
    int eof() {
        if (this->handle == 0) return 1;
        if (feof(this->handle)) return 1;
        /* feof() only fires after a failed read; check position too */
        long cur = ftell(this->handle);
        if (cur < 0) return 0;
        fseek(this->handle, 0, SEEK_END);
        long end = ftell(this->handle);
        fseek(this->handle, cur, SEEK_SET);
        return cur >= end;
    }

    /* ================================================================
       Navigation
       ================================================================ */

    /* Total size of the file in bytes, or -1 on error.
       Preserves the current read/write position. */
    long size() {
        if (!this->handle) return -1;
        long saved = ftell(this->handle);
        fseek(this->handle, 0, SEEK_END);
        long sz = ftell(this->handle);
        fseek(this->handle, saved, SEEK_SET);
        return sz;
    }

    /* Current byte offset, or -1 on error. */
    long pos() {
        if (!this->handle) return -1;
        return ftell(this->handle);
    }

    /* Seek to offset.  whence: 0 = start, 1 = current, 2 = end.
       Returns 0 on success, -1 on error. */
    int seek(long offset, int whence) {
        if (!this->handle) return -1;
        return fseek(this->handle, offset, whence);
    }

    /* ================================================================
       Explicit close / flush
       ================================================================ */

    /* Close the underlying stream immediately (the destructor will
       also close it, so this is optional). */
    void close() {
        if (this->handle) {
            fclose(this->handle);
            this->handle = 0;
        }
    }

    /* Flush any buffered output to the OS. */
    void flush() {
        if (this->handle) fflush(this->handle);
    }

    /* ================================================================
       Bulk read
       ================================================================ */

    /* Read from the current position to EOF into a malloc'd C string.
       The caller must free() the result.  Returns NULL on error or when
       the handle is not open. */
    char* read_all() {
        if (!this->handle) return 0;
        long start = ftell(this->handle);
        fseek(this->handle, 0, SEEK_END);
        long end = ftell(this->handle);
        fseek(this->handle, start, SEEK_SET);
        long sz = end - start;
        if (sz < 0) sz = 0;
        char *buf = (char *)malloc(sz + 1);
        if (!buf) return 0;
        long got = (long)fread(buf, 1, sz, this->handle);
        buf[got] = '\0';
        return buf;
    }

    /* Read the next line, stripping the trailing newline (CR+LF safe).
       Returns a malloc'd string that the caller must free().
       Returns NULL at EOF or on error. */
    char* read_line() {
        if (!this->handle || feof(this->handle)) return 0;
        size_t cap = 256;
        size_t len = 0;
        char *buf = (char *)malloc(cap);
        if (!buf) return 0;
        int c;
        int got_any = 0;
        while (1) {
            c = fgetc(this->handle);
            if (c == EOF) break;
            got_any = 1;
            if (c == '\n') break;
            if (c == '\r') continue;  /* skip CR in CRLF sequences */
            if (len + 2 > cap) {
                cap = cap * 2;
                char *tmp = (char *)realloc(buf, cap);
                if (!tmp) { free(buf); return 0; }
                buf = tmp;
            }
            buf[len] = (char)c;
            len = len + 1;
        }
        if (!got_any) { free(buf); return 0; }
        buf[len] = '\0';
        return buf;
    }

    /* ================================================================
       Write
       ================================================================ */

    /* Write a string to the file.  Returns 0 on success, -1 on error. */
    int write(char *data) {
        if (!this->handle || !data) return -1;
        return (fputs(data, this->handle) >= 0) ? 0 : -1;
    }

    /* Write a string followed by a newline.
       Returns 0 on success, -1 on error. */
    int writeln(char *data) {
        if (!this->handle || !data) return -1;
        if (fputs(data, this->handle) < 0) return -1;
        fputc('\n', this->handle);
        return 0;
    }

    /* ================================================================
       Streaming callbacks
       ================================================================ */

    /* Iterate every line in the file from byte 0, calling
         cb(line, line_number, ctx)
       for each one.  `line` is a temporary buffer owned by the method;
       copy it if you need it to outlive the callback.  `line_number` is
       1-based.  Return 0 from cb to stop early, non-zero to continue.
       Lines are stripped of their trailing newline; CRLF is handled. */
    void each_line(int (*cb)(char *, int, void *), void *ctx) {
        if (!this->handle) return;
        fseek(this->handle, 0, SEEK_SET);

        size_t cap = 512;
        char  *buf = (char *)malloc(cap);
        if (!buf) return;

        size_t len    = 0;
        int    lineno = 0;
        int    c;

        while ((c = fgetc(this->handle)) != EOF) {
            if (c == '\r') continue;           /* skip CR                  */
            if (c == '\n') {                   /* end of line              */
                buf[len] = '\0';
                lineno = lineno + 1;
                int go = cb(buf, lineno, ctx);
                len = 0;                       /* reset before possible break */
                if (!go) break;
                continue;
            }
            /* grow buffer if needed */
            if (len + 2 > cap) {
                cap = cap * 2;
                char *tmp = (char *)realloc(buf, cap);
                if (!tmp) break;
                buf = tmp;
            }
            buf[len] = (char)c;
            len = len + 1;
        }

        /* flush any trailing content not terminated by a newline */
        if (len > 0) {
            buf[len] = '\0';
            lineno = lineno + 1;
            cb(buf, lineno, ctx);
        }

        free(buf);
    }

    /* Iterate the file in fixed-size chunks from byte 0, calling
         cb(chunk, bytes_read, ctx)
       for each one.  `chunk` is a temporary buffer owned by the method.
       Return 0 from cb to stop early, non-zero to continue.
       chunk_size must be > 0. */
    void each_chunk(int (*cb)(char *, int, void *), void *ctx, int chunk_size) {
        if (!this->handle || chunk_size <= 0) return;
        fseek(this->handle, 0, SEEK_SET);

        char *buf = (char *)malloc(chunk_size + 1);
        if (!buf) return;

        int got;
        while ((got = (int)fread(buf, 1, chunk_size, this->handle)) > 0) {
            buf[got] = '\0';
            if (!cb(buf, got, ctx)) break;
        }

        free(buf);
    }
};

#endif /* FILE_H */
