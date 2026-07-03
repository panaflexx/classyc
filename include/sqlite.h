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

       QueryBuilder<T>   LINQ-style fluent SELECT builder -> typed entities
           new QueryBuilder<T>(db, table)
           .Where(col, op, value).OrderBy(col).OrderByDesc(col)
           .Take(n).Skip(n).Select(cols)
           .BuildSelectSql()   -> String
           .ToList()           -> List<T*>*   // owning; `defer delete`
           .FirstOrDefault()   -> T*          // heap entity or NULL
           .Count() / .Any()   -> int
       // T must provide: T() and void bindRow(dict). See EntityOps<T>.

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
#include "list.h"      /* List<T> — query() returns List<dict>*; also the single
                          source of truth for the dict_* runtime helpers below */
#include "dict_types.h" /* DictType tag (DICT_INT64 …) for bind(int, dict) */

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

    /* Convenience: first row only, with one-shot bound params (or NULL on no
       match).  The bound sibling of query_one(sql) — mirrors the
       query(sql, fmt, ...) binding rules.  Builds just the first row, so it
       does not leak the remainder of a result set. */
    dict query_one(const char* sql, const char* fmt, ...) {
        sqlite3_stmt* stmt = 0;
        int rc = sqlite3_prepare_v2(this->db, sql, -1, &stmt, 0);
        if (rc != SQLITE_OK) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "query_one/prepare: %s",
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

        int ncols = sqlite3_column_count(stmt);
        dict result = 0;
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            result = dict_create_object();
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
                dict_object_set(result, (char*)name, v);
            }
        } else if (rc != SQLITE_DONE) {
            this->last_err = rc;
            static char buf[512];
            snprintf(buf, sizeof(buf), "query_one/step: %s",
                     sqlite3_errmsg(this->db));
            sqlite3_finalize(stmt);
            throw(SqliteError, buf);
        }
        sqlite3_finalize(stmt);
        return result;
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

    /* Bind a dict value, dispatching on its runtime type tag.  val.type() reads
       the tag without unwrapping; the scalar payload is then extracted with an
       ordinary cast ((long)/(double)/(char*)) which DOES unwrap.  Covers the
       cases that arise from JSON-parsed bodies and SQLite row dicts;
       DICT_ARRAY / DICT_OBJECT bind as NULL. */
    int bind(int idx, dict val) {
        int rc;
        switch (val.type()) {
        case DICT_INT64:  rc = sqlite3_bind_int64(this->stmt, idx,
                                                  (sqlite3_int64)(long)val);           break;
        case DICT_NUMBER: rc = sqlite3_bind_double(this->stmt, idx, (double)val);      break;
        case DICT_BOOL:   rc = sqlite3_bind_int(this->stmt, idx, (int)(long)val);      break;
        case DICT_STRING: rc = sqlite3_bind_text(this->stmt, idx, (char*)val, -1,
                                                 (void(*)(void*))-1);                  break;
        default:          rc = sqlite3_bind_null(this->stmt, idx);                     break;
        }
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

/* -------------------------------------------------------------------------
   EntityOps<T> – schema-driven, generic active-record helper.  It generates
   INSERT / UPDATE / DELETE SQL from an entity's own metadata methods and
   materialises entities back from result-row dicts, so a class describes its
   table shape once and gets full persistence for free.

   Entity contract
   ───────────────
   * Read side (used by fromRow and by QueryBuilder<T>):
         T()                                // no-argument constructor
         void   bindRow(dict r)             // copy a SELECT row into fields
   * Write side (used by save / update / delete):
         String tableName()
         String insertColumns()             // "A,B,C"
         String insertPlaceholders()        // "?,?,?"
         String updateSet()                 // "A=?,B=?,C=?"
         int    columnCount()               // number of ? in updateSet()
         void   bindInsertValues(Statement* stmt)
         void   bindUpdateValues(Statement* stmt)
         int    getId()                     // primary key for UPDATE/DELETE

   Because ClassyC monomorphises every method of a generic class, EntityOps<T>
   only compiles the methods you actually instantiate for T.  QueryBuilder<T>
   deliberately does NOT go through EntityOps, so read-only entities that
   implement just T()/bindRow() work with QueryBuilder without providing the
   write-side metadata.

   Example:

       class User {
           int    id;
           String name;
           User() {}
           void bindRow(dict r) {
               this->id   = (int)(long) r.id;  // missing key -> 0 (lenient)
               this->name = r.name;            // String field owns a copy
           }
           // ...write-side metadata omitted; needed only for save/update/delete
       };

       User* u = EntityOps<User>.fromRow(row);   // caller owns the pointer
   ------------------------------------------------------------------------- */
class EntityOps<T> {
    static void save(T* e, Sqlite* db) {
        String sql = "INSERT INTO " + e->tableName() + " (" + e->insertColumns() +
                     ") VALUES (" + e->insertPlaceholders() + ")";
        owned Statement* stmt = db->prepare(sql);
        e->bindInsertValues(stmt);
        stmt->execute();
    }

    static void update(T* e, Sqlite* db) {
        String sql = "UPDATE " + e->tableName() + " SET " + e->updateSet() + " WHERE Id=?";
        owned Statement* stmt = db->prepare(sql);
        e->bindUpdateValues(stmt);
        stmt->bind(e->columnCount() + 1, e->getId());
        stmt->execute();
    }

    static void delete(T* e, Sqlite* db) {
        String sql = "DELETE FROM " + e->tableName() + " WHERE Id=?";
        owned Statement* stmt = db->prepare(sql);
        stmt->bind(1, e->getId());
        stmt->execute();
    }

    /* Inverse of save: materialise a fresh heap T from a SELECT-row dict.
       T must provide bindRow(dict) mapping its SQL columns to fields — the
       single place that mapping is written.  Because String fields bind with
       value semantics, the returned object owns private copies of its strings
       and is safe to keep after the source row dict is freed.  Caller owns the
       result (`delete` / `move`). */
    static T* fromRow(dict r) {
        T* e = new T();
        e->bindRow(r);
        return e;
    }
};

/* -------------------------------------------------------------------------
   QueryBuilder<T> – a small, LINQ-style fluent builder that composes a
   SELECT and materialises typed entities.

   ── Quick start ──────────────────────────────────────────────────────────

       // T must satisfy the EntityOps<T> contract (default ctor + bindRow).
       owned auto adults = new QueryBuilder<User>(db, "Users")
           .Where("age", ">", "18")
           .OrderBy("name")
           .Take(10)
           .ToList();                // -> List<User*>* (owns its User*)
       defer delete adults;          // frees the list AND every User

       for (auto u in adults)
           printf("%s (%d)\n", (char*)u->name, u->age);

       User* first = new QueryBuilder<User>(db, "Users")
           .Where("age", ">", "18").OrderBy("age").FirstOrDefault();
       defer delete first;           // may be NULL; delete NULL is a no-op

       int n = new QueryBuilder<User>(db, "Users")
           .Where("active", "=", "1").Count();

   ── API ──────────────────────────────────────────────────────────────────

       new QueryBuilder<T>(db, tableName)
       .Where(col, op, value)   // ANDs successive calls; value is quoted
       .OrderBy(col)            // ASC; repeatable (comma-joined)
       .OrderByDesc(col)        // DESC
       .Take(n)                 // LIMIT n
       .Skip(n)                 // OFFSET n
       .Select(cols)            // projection (default "*")
       .BuildSelectSql()        -> String   // the composed SQL
       .ToList()                -> List<T*>* // owning; caller deletes
       .FirstOrDefault()        -> T*        // heap entity or NULL
       .Count()                 -> int       // SELECT COUNT(*) with WHERE
       .Any()                   -> int       // Count() > 0

   Notes:
   * Where() interpolates `value` into a single-quoted literal.  It performs
     no escaping, so it is intended for trusted/programmatic values — use a
     prepared Statement for untrusted input.
   * ToList() returns an *owning* List<T*>: `delete list` frees the list and
     every entity in it.  FirstOrDefault() returns a standalone heap entity
     the caller owns.
   ------------------------------------------------------------------------- */
class QueryBuilder<T> {
    Sqlite* db;
    String  tableName;
    String  whereClause;
    String  orderByClause;
    int     limitCount;
    int     offsetCount;
    String  selectCols;
    int     hasWhere;
    int     hasOrderBy;

    QueryBuilder(Sqlite* db, String tableName) {
        this->db = db;
        this->tableName = tableName;
        this->whereClause = "";
        this->orderByClause = "";
        this->limitCount = -1;
        this->offsetCount = 0;
        this->selectCols = "*";
        this->hasWhere = 0;
        this->hasOrderBy = 0;
    }

    QueryBuilder<T>* Where(String column, String op, String value) {
        String condition = column + " " + op + " '" + value + "'";
        if (this->hasWhere) {
            this->whereClause = this->whereClause + " AND " + condition;
        } else {
            this->whereClause = condition;
            this->hasWhere = 1;
        }
        return this;
    }

    QueryBuilder<T>* OrderBy(String column) {
        if (this->hasOrderBy) {
            this->orderByClause = this->orderByClause + ", ";
        }
        this->orderByClause = this->orderByClause + column + " ASC";
        this->hasOrderBy = 1;
        return this;
    }

    QueryBuilder<T>* OrderByDesc(String column) {
        if (this->hasOrderBy) {
            this->orderByClause = this->orderByClause + ", ";
        }
        this->orderByClause = this->orderByClause + column + " DESC";
        this->hasOrderBy = 1;
        return this;
    }

    QueryBuilder<T>* Take(int count) {
        this->limitCount = count;
        return this;
    }

    QueryBuilder<T>* Skip(int count) {
        this->offsetCount = count;
        return this;
    }

    QueryBuilder<T>* Select(String columns) {
        this->selectCols = columns;
        return this;
    }

    String BuildSelectSql() {
        String sql = "SELECT " + this->selectCols + " FROM " + this->tableName;
        if (this->hasWhere) {
            sql = sql + " WHERE " + this->whereClause;
        }
        if (this->hasOrderBy) {
            sql = sql + " ORDER BY " + this->orderByClause;
        }
        if (this->limitCount >= 0) {
            sql = sql + " LIMIT " + this->limitCount;
        }
        if (this->offsetCount > 0) {
            sql = sql + " OFFSET " + this->offsetCount;
        }
        return sql;
    }

    /* Execute the composed SELECT and materialise an owning List<T*>.
       Each row becomes a heap T (default ctor + bindRow); the returned list
       `.owns()` them, so `delete list` frees the list and every T.  We build
       the entity inline rather than via EntityOps<T> so that read-only
       entities need only implement T() and bindRow(dict) — not the full
       write-side (save/update/delete) metadata contract. */
    List<T*>* ToList() {
        String sql = this->BuildSelectSql();
        List<dict>* rows = this->db->query(sql);
        List<T*>* result = new List<T*>().owns();
        if (!rows) return result;
        defer delete rows;
        for (int i = 0; i < rows->Count(); i++) {
            T* e = new T();
            e->bindRow(rows->Get(i));
            result->Add(e);
        }
        return result;
    }

    /* Execute and return the first entity as a standalone heap T* (caller
       owns it, `defer delete`), or NULL when there are no rows. */
    T* FirstOrDefault() {
        this->Take(1);
        String sql = this->BuildSelectSql();
        List<dict>* rows = this->db->query(sql);
        if (!rows) return NULL;
        defer delete rows;
        if (rows->Count() == 0) return NULL;
        T* e = new T();
        e->bindRow(rows->Get(0));
        return e;
    }

    /* SELECT COUNT(*) honouring the current WHERE clause. */
    int Count() {
        String sql = "SELECT COUNT(*) FROM " + this->tableName;
        if (this->hasWhere) {
            sql = sql + " WHERE " + this->whereClause;
        }
        dict row = this->db->query_one(sql);
        if (!row) return 0;
        return (int)(long)row["COUNT(*)"];
    }

    /* True iff at least one row matches the current WHERE clause. */
    int Any() {
        return this->Count() > 0;
    }
};

#endif /* SQLITE_CLASSYC_H */
