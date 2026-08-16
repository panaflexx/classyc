CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, val REAL);
INSERT INTO t(name,val) VALUES('alpha',1.5),('beta',2.5),('gamma',3.5);
SELECT count(*) AS rows, sum(val) AS total FROM t;
SELECT id, name, val FROM t ORDER BY val DESC;
SELECT name FROM t WHERE val > 2.0;
.mode box
SELECT upper(name)||'='||printf('%.2f',val) AS kv FROM t;
