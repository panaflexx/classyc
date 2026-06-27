/* sketch-owned-service.cy — stress test: owned / move / readonly in a
 * "read from a DB and serve requests" controller flow (cf. classy-controller-like.cy).
 *
 * Ownership design under test:
 *   - Repo OWNS the canonical User objects (an `.owns()` List<User*>).
 *   - New users are created as `owned` locals and `move`d into the repo
 *     (ownership hand-off; the repo frees them).
 *   - Handlers borrow users via `readonly` and return an `owned` Response that
 *     holds a non-owning view of the user.
 *   - The serve loop holds each Response as an `owned` per-iteration local that
 *     auto-deletes at the bottom of the iteration (no `defer delete`).
 *   - A POST handler creates an `owned` temp and EITHER moves it into the repo
 *     (success) OR returns early on validation failure (the temp must be freed).
 *
 * Stored fields use only `int` / `const char*` literals so valgrind sees pure
 * object-ownership behaviour (no String-arena noise).  Exact destructor counts
 * are asserted, so any leak or double free fails the test.
 *
 * Run:  ./bin/classyc -g -I include sketch/sketch-owned-service.cy -eg
 */
#include <stdio.h>
#include <string.h>
#include "list.h"

int user_dtors = 0, resp_dtors = 0;
int passed = 0, failed = 0;
void check(int cond, const char* label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

class User {
    int id;
    const char* name;
    const char* role;
    User(int id, const char* name, const char* role) {
        this.id = id; this.name = name; this.role = role;
    }
    ~User() { user_dtors++; }
};

class Response {
    int status;
    const char* reason;
    User* user;          /* BORROW (readonly view) or NULL — never owned here */
    Response(int status, const char* reason, User* user) {
        this.status = status; this.reason = reason; this.user = user;
    }
    ~Response() { resp_dtors++; }   /* must NOT delete this.user */
};

/* The "database": owns the canonical User rows. */
class Repo {
    List<User*>* users;
    Repo() { this.users = new List<User*>().owns(); }
    ~Repo() { delete this.users; }              /* frees every owned user */
    void add(User* u) { this.users->Add(u); }   /* takes ownership of u */
    User* find(int id) {
        for (int i = 0; i < this.users->Count(); i++) {
            User* u = this.users->Get(i);
            if (u->id == id) return u;          /* borrow */
        }
        return NULL;
    }
    int count() { return this.users->Count(); }
};

/* GET /users/{id}: borrow the user, return an owned Response holding the view. */
Response* getUser(Repo* repo, int id) {
    auto u = readonly repo->find(id);
    if (u == NULL) return new Response(404, "Not Found", NULL);
    return new Response(200, "OK", u);
}

/* POST /users: validate, then MOVE the new user into the repo on success.
   On the validation-failure path the owned temp `u` must still be released. */
Response* createUser(Repo* repo, int id, const char* name, const char* role) {
    owned auto u = new User(id, name, role);
    if (name == NULL || strlen(name) == 0)
        return new Response(400, "Bad Request", NULL);   /* u released on this path */
    repo->add(move u);                                   /* ownership -> repo */
    return new Response(201, "Created", repo->find(id));
}

void serve(Repo* repo) {
    /* a batch of GETs: some hit, one misses */
    int ids[5] = { 1, 3, 5, 99, 2 };
    for (auto id in ids) {
        owned auto resp = getUser(repo, id);
        if (resp->user != NULL)
            printf("    GET %d -> %d %s : %s (%s)\n",
                   id, resp->status, resp->reason, resp->user->name, resp->user->role);
        else
            printf("    GET %d -> %d %s\n", id, resp->status, resp->reason);
    }   /* resp auto-deleted each iteration; does NOT delete resp->user */

    /* one valid POST (moves a user in) and one invalid POST (temp released) */
    {
        owned auto ok  = createUser(repo, 6, "Frank", "viewer");
        owned auto bad = createUser(repo, 7, "", "viewer");
        printf("    POST Frank -> %d %s\n", ok->status, ok->reason);
        printf("    POST ''    -> %d %s\n", bad->status, bad->reason);
    }   /* ok, bad auto-deleted here */

    check(repo->count() == 6, "repo holds 6 users after one successful POST");
}

void run() {
    owned auto repo = new Repo();
    const char* names[5] = { "Alice", "Bob", "Carol", "Dan", "Eve" };
    const char* roles[5] = { "admin", "editor", "viewer", "viewer", "editor" };
    for (int i = 0; i < 5; i++) {
        owned auto u = new User(i + 1, names[i], roles[i]);
        repo->add(move u);                       /* hand ownership to the repo */
    }
    check(repo->count() == 5, "repo seeded with 5 users");
    serve(repo);
    /* repo auto-deleted at end of run() -> frees all owned users */
}

int main() {
    printf("=== owned / move / readonly service stress test ===\n\n");
    run();

    /* Users created: 5 seeded + 1 (successful POST) + 1 (failed-POST temp) = 7.
       All 7 must be freed exactly once: 6 by the repo, 1 on the error path. */
    check(user_dtors == 7, "all 7 users freed exactly once (no leak, no double free)");
    /* Responses: 5 GET + 2 POST = 7, each freed at its owning scope. */
    check(resp_dtors == 7, "all 7 responses freed exactly once");

    printf("\n  user_dtors=%d (want 7)   resp_dtors=%d (want 7)\n", user_dtors, resp_dtors);
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
