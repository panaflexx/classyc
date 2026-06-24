/* =========================================================================
   include/sqlite.h — a small, classy SQLite binding for ClassyC

   Header-only.  Open a database, run statements (one-shot or prepared),
   iterate result rows as dicts, bind-cast rows to typed classes, and use
   transactions as scoped RAII objects.

   ── Quick start ──────────────────────────────────────────────────────────

       #include "include/sqlite.h"

       Sqlite* db = Sqlite.open(":memory:");
       defer delete db;

       db->execute("CREATE TABLE customers (id INTEGER PRIMARY KEY, "
                   "  firstName TEXT, lastName TEXT, email TEXT)");

       try {
           db->execute("INSERT INTO customers VALUES (?, ?, ?, ?)",
                       "isss", 1, "Ada", "Lovelace", "ada@analytical.engine");
       } catch (SqliteError e) {
           printf("db error: %s\n", e.msg);
       }

       List<dict>* rows = db->query(
           "SELECT * FROM customers WHERE id >= ? ORDER BY id", "i", 1);
       defer delete rows;
       for (auto r in rows) {
           Customer c = (Customer) r;        // bind row -> typed class
           printf("#%d %s %s\n", c.id, (char*)c.firstName, (char*)c.lastName);
       }

   ── Build / run ──────────────────────────────────────────────────────────

       classyc -I include -l sqlite3 your-app.cy -eg

   ── API surface ──────────────────────────────────────────────────────────

       Sqlite.open(path)                    -> Sqlite*   // NULL on failure
       db->execute(sql)                     -> int       // rows affected
       db->execute(sql, fmt, ...)           -> int       // bound, throws on error
       db->query(sql)                       -> List<dict>*
       db->query(sql, fmt, ...)             -> List<dict>*
       db->query_one(sql)                   -> dict     // first row or NULL
       db->prepare(sql)                     -> Statement*
       db->begin()                          -> Transaction*
       db->lastInsertRowId()                -> long
       db->changes()                        -> int
       db->errmsg()                         -> const char*

       Statement                            (caller `defer delete`s it)
           ->bind(idx, int|long|double|const char*)  -> int rc
           ->bindNull(idx)                  -> int rc
           ->step()                         -> int  // SQLITE_ROW / SQLITE_DONE / err
           ->execute()                      -> int  // throws on error
           ->query()                        -> List<dict>*
           ->query_one()                    -> dict
           ->reset()                        -> int rc
           ->clearBindings()                -> int rc

       Transaction                          (caller `defer delete`s it)
           ->commit()                       // marks inactive
           ->rollback()                     // safe after commit (no-op)
           ~Transaction()                    // ROLLBACK if commit() never ran

   ── Format string for execute(sql, fmt, ...) / query(sql, fmt, ...) ──────

       i   int      -> sqlite3_bind_int
       l   long     -> sqlite3_bind_int64
       d   double   -> sqlite3_bind_double      (alias: 'f')
       f   double   -> sqlite3_bind_double
       s   char*    -> sqlite3_bind_text (SQLITE_TRANSIENT; sqlite copies)
       n   (none)   -> sqlite3_bind_null         (no vararg consumed)

   Indices in Statement.bind() / SQL `?` are 1-based, matching the C API.

   ── Errors / exceptions ─────────────────────────────────────────────────

       The convenience paths (`execute(sql)`, `execute(sql, fmt, ...)`,
       `query(...)`, `prepare(...)`, `Statement::execute/query`) throw
       SqliteError on failure, with `e.msg` carrying the underlying
       sqlite3_errmsg() text.  The granular `Statement::bind()` calls return
       the rc instead of throwing, so granular code can choose its own
       error handling.

   ── Notes ────────────────────────────────────────────────────────────────

   * SQL NULL on read becomes JSON null in the dict (`dict_create_null`),
     so `"col" in row` and `(T) row` bind-cast behave correctly.
   * Exception messages are formatted into a static buffer; single-threaded
     by construction.  Threaded callers should serialise access to one db.
   * No method-template binding yet — the format-string variadic is the
     Pythonic-feel path; `Statement` + per-type `bind()` is the explicit path.
   ========================================================================= */

#ifndef SQLITE_CLASSYC_H
#define SQLITE_CLASSYC_H

#include <sqlite3.h>
#include <stdarg.h>
#include <stdio.h>
#include "list.h"      /* List<T> — query() returns List<dict>* */

/* ---- runtime dict helpers (linked via import_resolver) ---- */
dict dict_create_object();
dict dict_create_null();
dict dict_create_int64(long n);
dict dict_create_number(double n);
dict dict_create_string(char *s);
int  dict_object_set(dict obj, char *key, dict val);

/* ---- user-defined exception class for SQLite errors ----
   Throw with:  throw(SqliteError, "msg");
   Catch with:  catch (SqliteError e) { ... e.id, e.msg ... } */
enum {
    SqliteError = 200
};

/* -------------------------------------------------------------------------
   Sqlite – connection wrapper
   ------------------------------------------------------------------------- */
class Sqlite {
    sqlite3* db;
    int      last_err;

    /* Private-ish ctor.  Code outside this header should use the
       Sqlite.open() static factory which handles open-failure cleanup. */
    Sqlite(sqlite3* h) { this->db = h; this->last_err = 0; }

    /* Factory: returns a heap-allocated Sqlite on success, or NULL on
       failure.  Caller owns the returned pointer and should pair the call
       with `defer delete db;`. */
    static Sqlite* open(const char* path) {
        sqlite3* h = 0;
        int rc = sqlite3_open(path, &h);
        if (rc != SQLITE_OK) {
            if (h) sqlite3_close(h);
            return 0;
        }
        return new Sqlite(h);
    }

    /* Destructor — runs on `delete db` or `defer delete db`. */
    ~Sqlite() {
        if (this->db) {
            sqlite3_close(this->db);
            this->db = 0;
        }
    }

    /* ---------------------------------------------------------------------
       INSERT / UPDATE / DELETE / DDL — returns number of rows affected on
       success (sqlite3_changes(db) after the step), or -1 on error.

       NOTE: no parameter binding yet.  Embed values directly in the SQL
       for now (safe for trusted/literal SQL; rewrite once a real binder
       lands).
       --------------------------------------------------------------------- */
    int execute(const char* sql) {
        char* errmsg = 0;
        int rc = sqlite3_exec(this->db, sql, 0, 0, &errmsg);
        if (rc != SQLITE_OK) {
            this->last_err = rc;
            char* msg = errmsg ? errmsg : (char*)sqlite3_errmsg(this->db);
            /* Copy to a stable buffer before freeing errmsg (the exception
               machinery keeps msg as a pointer; sqlite3_free invalidates it). */
            static char buf[512];
            snprintf(buf, sizeof(buf), "execute: %s", msg ? msg : "(no msg)");
            if (errmsg) sqlite3_free(errmsg);
            throw(SqliteError, buf);
        }
        return sqlite3_changes(this->db);
    }

    /* ---------------------------------------------------------------------
       Raw query — returns a List<dict>* the caller owns (or NULL on
       failure).  Pair with `defer delete rows;` in the caller.

       Each row becomes a `dict` whose keys are the column names and whose
       values are dict_create_int64 / _number / _string / NULL based on the
       column's runtime type.
       --------------------------------------------------------------------- */
    List<dict>* query(const char* sql) {
        sqlite3_stmt* stmt = 0;
        int rc = sqlite3_prepare_v2(this->db, sql, -1, &stmt, 0);
        if (rc != SQLITE_OK) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "query/prepare: %s",
                     sqlite3_errmsg(this->db));
            if (stmt) sqlite3_finalize(stmt);
            throw(SqliteError, buf);
        }

        List<dict>* rows = new List<dict>();
        int ncols = sqlite3_column_count(stmt);

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            dict row = dict_create_object();
            for (int i = 0; i < ncols; i++) {
                const char* name = sqlite3_column_name(stmt, i);
                int t = sqlite3_column_type(stmt, i);
                dict v = 0;
                if (t == SQLITE_INTEGER) {
                    v = dict_create_int64((long)sqlite3_column_int64(stmt, i));
                } else if (t == SQLITE_FLOAT) {
                    v = dict_create_number(sqlite3_column_double(stmt, i));
                } else if (t == SQLITE_TEXT) {
                    v = dict_create_string((char*)sqlite3_column_text(stmt, i));
                } else {
                    /* SQLITE_NULL or SQLITE_BLOB — preserve as JSON null so
                       (T) bind-cast and `"key" in row` work correctly. */
                    v = dict_create_null();
                }
                dict_object_set(row, (char*)name, v);
            }
            rows->Add(row);
        }

        if (rc != SQLITE_DONE) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "query/step: %s",
                     sqlite3_errmsg(this->db));
            sqlite3_finalize(stmt);
            delete rows;
            throw(SqliteError, buf);
        }
        sqlite3_finalize(stmt);
        return rows;
    }

    /* Convenience: first row only (or NULL on no match / error). */
    dict query_one(const char* sql) {
        List<dict>* rows = this->query(sql);
        if (!rows || rows->Count() == 0) {
            if (rows) delete rows;
            return 0;
        }
        dict r = rows->Get(0);
        /* NOTE: we leak the rest of the list here in the sketch.  A real
           impl would either copy the row or detach it from the list. */
        delete rows;
        return r;
    }

    /* Prepared statement for hot paths.  Caller owns the returned
       Statement* and should `defer delete stmt;` it.  Throws SqliteError
       on a malformed statement. */
    Statement* prepare(const char* sql) {
        sqlite3_stmt* s = 0;
        int rc = sqlite3_prepare_v2(this->db, sql, -1, &s, 0);
        if (rc != SQLITE_OK) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "prepare: %s",
                     sqlite3_errmsg(this->db));
            if (s) sqlite3_finalize(s);
            throw(SqliteError, buf);
        }
        return new Statement(this, s);
    }

    /* sqlite3_last_insert_rowid — the auto-increment id of the most recent
       successful INSERT on this connection. */
    long lastInsertRowId() {
        return (long)sqlite3_last_insert_rowid(this->db);
    }

    /* sqlite3_changes — number of rows modified by the most recent
       INSERT/UPDATE/DELETE on this connection. */
    int changes() {
        return sqlite3_changes(this->db);
    }

    /* ---------------------------------------------------------------------
       One-shot bound execute / query (printf-ish format string).

           db->execute("INSERT ... VALUES (?, ?, ?)", "isd", 1, "hello", 3.14);
           List<dict>* rows = db->query(
               "SELECT * FROM t WHERE x=? AND y=?", "is", 42, "NY");

       Format chars (all 1-indexed):
           i   int      -> sqlite3_bind_int
           l   long     -> sqlite3_bind_int64
           d   double   -> sqlite3_bind_double   (alias: 'f')
           f   double   -> sqlite3_bind_double
           s   char*    -> sqlite3_bind_text (SQLITE_TRANSIENT, sqlite copies)
           n   (none)   -> sqlite3_bind_null    (no va_arg consumed)

       Throws SqliteError on prepare/bind/step failure.
       --------------------------------------------------------------------- */
    int execute(const char* sql, const char* fmt, ...) {
        sqlite3_stmt* stmt = 0;
        int rc = sqlite3_prepare_v2(this->db, sql, -1, &stmt, 0);
        if (rc != SQLITE_OK) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "execute/prepare: %s",
                     sqlite3_errmsg(this->db));
            if (stmt) sqlite3_finalize(stmt);
            throw(SqliteError, buf);
        }
        va_list ap;
        va_start(ap, fmt);
        for (int i = 0; fmt[i] != 0; i++) {
            int idx = i + 1;
            char c = fmt[i];
            if (c == 'i') {
                sqlite3_bind_int(stmt, idx, va_arg(ap, int));
            } else if (c == 'l') {
                sqlite3_bind_int64(stmt, idx,
                                   (sqlite3_int64)va_arg(ap, long));
            } else if (c == 'd' || c == 'f') {
                sqlite3_bind_double(stmt, idx, va_arg(ap, double));
            } else if (c == 's') {
                sqlite3_bind_text(stmt, idx, va_arg(ap, char*), -1,
                                  (void(*)(void*))-1);
            } else if (c == 'n') {
                sqlite3_bind_null(stmt, idx);
            }
        }
        va_end(ap);
        rc = sqlite3_step(stmt);
        int n = sqlite3_changes(this->db);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "execute/step: %s",
                     sqlite3_errmsg(this->db));
            throw(SqliteError, buf);
        }
        return n;
    }

    List<dict>* query(const char* sql, const char* fmt, ...) {
        sqlite3_stmt* stmt = 0;
        int rc = sqlite3_prepare_v2(this->db, sql, -1, &stmt, 0);
        if (rc != SQLITE_OK) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "query/prepare: %s",
                     sqlite3_errmsg(this->db));
            if (stmt) sqlite3_finalize(stmt);
            throw(SqliteError, buf);
        }
        va_list ap;
        va_start(ap, fmt);
        for (int i = 0; fmt[i] != 0; i++) {
            int idx = i + 1;
            char c = fmt[i];
            if (c == 'i') {
                sqlite3_bind_int(stmt, idx, va_arg(ap, int));
            } else if (c == 'l') {
                sqlite3_bind_int64(stmt, idx,
                                   (sqlite3_int64)va_arg(ap, long));
            } else if (c == 'd' || c == 'f') {
                sqlite3_bind_double(stmt, idx, va_arg(ap, double));
            } else if (c == 's') {
                sqlite3_bind_text(stmt, idx, va_arg(ap, char*), -1,
                                  (void(*)(void*))-1);
            } else if (c == 'n') {
                sqlite3_bind_null(stmt, idx);
            }
        }
        va_end(ap);

        List<dict>* rows = new List<dict>();
        int ncols = sqlite3_column_count(stmt);
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            dict row = dict_create_object();
            for (int i = 0; i < ncols; i++) {
                const char* name = sqlite3_column_name(stmt, i);
                int t = sqlite3_column_type(stmt, i);
                dict v = 0;
                if (t == SQLITE_INTEGER) {
                    v = dict_create_int64((long)sqlite3_column_int64(stmt, i));
                } else if (t == SQLITE_FLOAT) {
                    v = dict_create_number(sqlite3_column_double(stmt, i));
                } else if (t == SQLITE_TEXT) {
                    v = dict_create_string((char*)sqlite3_column_text(stmt, i));
                } else {
                    v = dict_create_null();
                }
                dict_object_set(row, (char*)name, v);
            }
            rows->Add(row);
        }
        if (rc != SQLITE_DONE) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "query/step: %s",
                     sqlite3_errmsg(this->db));
            sqlite3_finalize(stmt);
            delete rows;
            throw(SqliteError, buf);
        }
        sqlite3_finalize(stmt);
        return rows;
    }

    /* Begin a transaction.  Returns a heap Transaction* the caller owns.

       Idiomatic use:

           Transaction* tx = db->begin();
           defer delete tx;        // dtor rolls back if commit() never ran
           // ...do work...
           tx->commit();
    */
    Transaction* begin() {
        return new Transaction(this);
    }

    /* Last error message from the underlying connection. */
    const char* errmsg() {
        return sqlite3_errmsg(this->db);
    }
};

/* -------------------------------------------------------------------------
   Statement – prepared-statement wrapper.

   bind() is overloaded so calls dispatch on the argument type:

       stmt->bind(1, 42);            // int     -> sqlite3_bind_int
       stmt->bind(2, 9000000000L);   // long    -> sqlite3_bind_int64
       stmt->bind(3, 3.14);          // double  -> sqlite3_bind_double
       stmt->bind(4, "hello");       // char*   -> sqlite3_bind_text
       stmt->bindNull(5);            // NULL

   Indices are 1-based, matching the underlying sqlite3 C API.
   ------------------------------------------------------------------------- */
class Statement {
    sqlite3_stmt* stmt;
    Sqlite*       owner;
    int           last_err;

    Statement(Sqlite* o, sqlite3_stmt* s) {
        this->owner = o;
        this->stmt = s;
        this->last_err = 0;
    }

    ~Statement() {
        if (this->stmt) {
            sqlite3_finalize(this->stmt);
            this->stmt = 0;
        }
    }

    /* ---- bind overloads (1-indexed) ---- */

    int bind(int idx, int val) {
        int rc = sqlite3_bind_int(this->stmt, idx, val);
        if (rc != SQLITE_OK) this->last_err = rc;
        return rc;
    }

    int bind(int idx, long val) {
        int rc = sqlite3_bind_int64(this->stmt, idx, (sqlite3_int64)val);
        if (rc != SQLITE_OK) this->last_err = rc;
        return rc;
    }

    int bind(int idx, double val) {
        int rc = sqlite3_bind_double(this->stmt, idx, val);
        if (rc != SQLITE_OK) this->last_err = rc;
        return rc;
    }

    int bind(int idx, const char* val) {
        /* SQLITE_TRANSIENT = (sqlite3_destructor_type)-1: ask sqlite to copy. */
        int rc = sqlite3_bind_text(this->stmt, idx, val, -1,
                                   (void(*)(void*))-1);
        if (rc != SQLITE_OK) this->last_err = rc;
        return rc;
    }

    int bindNull(int idx) {
        int rc = sqlite3_bind_null(this->stmt, idx);
        if (rc != SQLITE_OK) this->last_err = rc;
        return rc;
    }

    /* Reset the statement so it can be re-bound and re-run.  Does NOT clear
       previously-bound values — use clearBindings() for that. */
    int reset() {
        return sqlite3_reset(this->stmt);
    }

    int clearBindings() {
        return sqlite3_clear_bindings(this->stmt);
    }

    /* Raw step — returns SQLITE_ROW, SQLITE_DONE, or an error rc. */
    int step() {
        return sqlite3_step(this->stmt);
    }

    /* Run once.  Returns number of rows affected (sqlite3_changes).
       Resets the statement on the way out so it's ready for the next set
       of binds.  Throws SqliteError on a step failure. */
    int execute() {
        sqlite3* h = sqlite3_db_handle(this->stmt);
        int rc = sqlite3_step(this->stmt);
        sqlite3_reset(this->stmt);
        if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "stmt/execute: %s",
                     sqlite3_errmsg(h));
            throw(SqliteError, buf);
        }
        return sqlite3_changes(h);
    }

    /* Run-to-completion, materializing all rows into a List<dict>*.
       Resets the statement on the way out.  Throws SqliteError on a step
       failure (the in-progress list is freed first). */
    List<dict>* query() {
        List<dict>* rows = new List<dict>();
        int ncols = sqlite3_column_count(this->stmt);
        int rc;
        while ((rc = sqlite3_step(this->stmt)) == SQLITE_ROW) {
            dict row = dict_create_object();
            for (int i = 0; i < ncols; i++) {
                const char* name = sqlite3_column_name(this->stmt, i);
                int t = sqlite3_column_type(this->stmt, i);
                dict v = 0;
                if (t == SQLITE_INTEGER) {
                    v = dict_create_int64(
                        (long)sqlite3_column_int64(this->stmt, i));
                } else if (t == SQLITE_FLOAT) {
                    v = dict_create_number(
                        sqlite3_column_double(this->stmt, i));
                } else if (t == SQLITE_TEXT) {
                    v = dict_create_string(
                        (char*)sqlite3_column_text(this->stmt, i));
                } else {
                    /* SQLITE_NULL / SQLITE_BLOB -> JSON null */
                    v = dict_create_null();
                }
                dict_object_set(row, (char*)name, v);
            }
            rows->Add(row);
        }
        if (rc != SQLITE_DONE) {
            this->last_err = rc;
            sqlite3* h = sqlite3_db_handle(this->stmt);
            static char buf[512];
            snprintf(buf, sizeof(buf), "stmt/query: %s",
                     sqlite3_errmsg(h));
            sqlite3_reset(this->stmt);
            delete rows;
            throw(SqliteError, buf);
        }
        sqlite3_reset(this->stmt);
        return rows;
    }

    /* Convenience: first row only.  Returns NULL on no match / error.
       Resets the statement. */
    dict query_one() {
        List<dict>* rows = this->query();
        if (!rows || rows->Count() == 0) {
            if (rows) delete rows;
            return 0;
        }
        dict r = rows->Get(0);
        delete rows;
        return r;
    }
};

/* -------------------------------------------------------------------------
   Transaction – tiny RAII helper.  Lives on the heap so it slots into the
   uniform `T* x = ...; defer delete x;` idiom; the destructor rolls back
   if commit() never ran.

       Transaction* tx = db->begin();
       defer delete tx;         // rolls back automatically if no commit()
       ... work ...
       tx->commit();
   ------------------------------------------------------------------------- */
class Transaction {
    Sqlite* db;
    int     active;

    Transaction(Sqlite* d) {
        this->db = d;
        this->active = 1;
        d->execute("BEGIN");
    }

    ~Transaction() {
        if (this->active) {
            this->db->execute("ROLLBACK");
            this->active = 0;
        }
    }

    void commit() {
        if (this->active) {
            this->db->execute("COMMIT");
            this->active = 0;
        }
    }

    void rollback() {
        if (this->active) {
            this->db->execute("ROLLBACK");
            this->active = 0;
        }
    }
};

#endif /* SQLITE_CLASSYC_H */
