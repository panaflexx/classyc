/* Test 127: Exception handling with finally-like patterns */
#include <stdio.h>

class Connection {
    String name;
    int closed;
    Connection(String n) { this.name = n; this.closed = 0; printf("connect %s\n", this.name); }
    ~Connection() { if (!this.closed) this.close(); }
    void close() { if (!this.closed) { this.closed = 1; printf("close %s\n", this.name); } }
    void query(String sql) { printf("%s: %s\n", this.name, sql); }
};

void withConnection(String name, void (*body)(Connection*)) {
    Connection* conn = new Connection(name);
    defer conn->close();
    body(conn);
}

int main() {
    // RAII with defer
    withConnection("db1", (Connection* c) => {
        c->query("SELECT * FROM users");
        c->query("SELECT * FROM posts");
    });

    // Manual try/finally pattern
    Connection* c2 = new Connection("db2");
    int success = 0;
    try {
        c2->query("BEGIN");
        c2->query("INSERT ...");
        c2->query("COMMIT");
        success = 1;
    } catch (Exception e) {
        c2->query("ROLLBACK");
        printf("error: %s\n", e.msg);
    }
    c2->close();
    printf("success: %d\n", success);

    // Nested connections with defer
    {
        Connection* outer = new Connection("outer");
        defer outer->close();
        {
            Connection* inner = new Connection("inner");
            defer inner->close();
            inner->query("inner work");
        }
        outer->query("outer work");
    }

    return 0;
}
