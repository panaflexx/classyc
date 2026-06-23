/* =========================================================================
   sketch/sqlite-classyc.h — proposed SQLite binding for ClassyC
   =========================================================================

   Design constraints (what actually exists today):
     • No method templates (template<T> List<T>* query(sql, ...))
     • We do have the cast binder:   User u = (User) some_dict;
     • We have List<T>, Map<K,V>, Set<T>, defer delete, arenas, String
     • dict is the universal dynamic container (rows come out as dicts)
     • Class instances live on the heap: every factory / accessor returns
       a `T*` produced by `new T(...)`.  Value-class returns and the bare
       `T(args)` expression form are not in the language.
     • Mutually-referencing classes (Sqlite ⇄ Statement ⇄ Transaction) are
       fine — the compiler's parse + check + gen layers all handle
       forward references between classes in the same translation unit.

   API shape:

       Sqlite* db = Sqlite.open("data.sqlite");   // or ":memory:"
       defer delete db;

       db->execute("CREATE TABLE ...");
       db->execute("INSERT INTO ... VALUES (...)");

       List<dict>* rows = db->query("SELECT ...");
       defer delete rows;
       for (auto r in rows) {
           Customer c = (Customer) r;        // strict bind
       }

       Transaction* tx = db->begin();
       defer delete tx;                      // dtor rolls back if no commit
       db->execute("UPDATE ...");
       tx->commit();

   What is and isn't implemented yet:
     execute(sql)         — real, via sqlite3_exec
     query(sql)           — real, materializes each row into a dict
                            (INTEGER/REAL/TEXT/NULL columns supported)
     query_one(sql)       — real, returns first row's dict (or NULL)
     prepare()/Statement  — still a stub
     Parameter binding    — NOT YET; embed values directly in the SQL string
                            until we add a real binder.  This is fine for an
                            in-memory smoke test but obviously not for
                            production code (SQL injection).
   ========================================================================= */

#ifndef SQLITE_CLASSYC_H
#define SQLITE_CLASSYC_H

#include <sqlite3.h>
#include <stdarg.h>
#include <stdio.h>
#include "list.h"      /* List<T> — query() returns List<dict>* */

/* ---- runtime dict helpers (linked via import_resolver) ---- */
dict dict_create_object();
dict dict_create_int64(long n);
dict dict_create_number(double n);
dict dict_create_string(char *s);
int  dict_object_set(dict obj, char *key, dict val);

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
            if (errmsg) {
                printf("sqlite execute failed: %s\n", errmsg);
                sqlite3_free(errmsg);
            } else {
                printf("sqlite execute failed (rc=%d)\n", rc);
            }
            return -1;
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
            printf("sqlite prepare failed (rc=%d)\n", rc);
            if (stmt) sqlite3_finalize(stmt);
            return 0;
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
                } else if (t == SQLITE_NULL) {
                    v = dict_create_string("");
                } else {
                    /* SQLITE_BLOB — surface as empty string for now */
                    v = dict_create_string("");
                }
                dict_object_set(row, (char*)name, v);
            }
            rows->Add(row);
        }

        if (rc != SQLITE_DONE) {
            this->last_err = rc;
            printf("sqlite step failed (rc=%d)\n", rc);
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

    /* Prepared statement for hot paths — still a stub. */
    Statement* prepare(const char* sql) {
        return 0;
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
   Statement – prepared-statement wrapper (still a stub)
   ------------------------------------------------------------------------- */
class Statement {
    sqlite3_stmt* stmt;
    Sqlite*       owner;

    Statement(Sqlite* o, sqlite3_stmt* s) { this->owner = o; this->stmt = s; }

    ~Statement() {
        if (this->stmt) {
            sqlite3_finalize(this->stmt);
            this->stmt = 0;
        }
    }

    List<dict>* query()     { return 0; }   /* sketch */
    dict        query_one() { return 0; }   /* sketch */
    int         execute()   { return 0; }   /* sketch */
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
