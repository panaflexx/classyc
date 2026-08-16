#include <stdio.h>
#include "sqlite3.h"

static int cb(void *unused, int ncol, char **vals, char **names) {
  (void)unused;
  for (int i = 0; i < ncol; i++)
    printf("%s=%s%s", names[i], vals[i] ? vals[i] : "NULL", i+1<ncol ? "  " : "\n");
  return 0;
}

static void run(sqlite3 *db, const char *sql) {
  char *err = 0;
  printf("> %s\n", sql);
  if (sqlite3_exec(db, sql, cb, 0, &err) != SQLITE_OK) {
    printf("  ERROR: %s\n", err ? err : "?");
    sqlite3_free(err);
  }
}

int main(void) {
  sqlite3 *db;
  printf("SQLite %s via classyc JIT\n", sqlite3_libversion());
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) { printf("open failed\n"); return 1; }
  run(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, val REAL)");
  run(db, "INSERT INTO t(name,val) VALUES('alpha',1.5),('beta',2.5),('gamma',3.5)");
  run(db, "SELECT count(*) AS rows, sum(val) AS total FROM t");
  run(db, "SELECT id,name,val FROM t WHERE val>2.0 ORDER BY val DESC");
  run(db, "SELECT upper(name)||'='||printf('%.2f',val) AS kv FROM t ORDER BY id");
  run(db, "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<5) SELECT group_concat(x,',') AS fib FROM c");
  sqlite3_close(db);
  printf("OK\n");
  return 0;
}
