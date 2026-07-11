/* _proto_controller.c — validate fixed patterns for the real controller */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── minimal List<dict> ── */
class List<T> {
    T*  data; int length; int capacity;
    List() { this.length=0; this.capacity=4; this.data=(T*)malloc(sizeof(T)*4); }
    ~List() { if(this.data) free((void*)this.data); }
    int Count() { return this.length; }
    T   Get(int i) { return this.data[i]; }
    void Add(T item) {
        if (this.length >= this.capacity) {
            int nc = this.capacity*2; T* nd=(T*)malloc(sizeof(T)*nc);
            for(int i=0;i<this.length;i++) nd[i]=this.data[i];
            free((void*)this.data); this.data=nd; this.capacity=nc;
        }
        this.data[this.length++] = item;
    }
};

/* ── query-string helper ── */
String qparam(String qs, String key) {
    String needle = key + "=";
    int p = (int) qs.find(needle);
    if (p < 0) return NULL;
    String rest = qs.substr(p + (int)needle.length(),
                            (int)qs.length() - p - (int)needle.length());
    int amp = (int) rest.find("&");
    if (amp >= 0) return rest.substr(0, amp);
    return rest;
}

int main() {
    /* 1. d.json() now returns String — works in f-strings and + */
    dict alice = { "id": 1, "name": "Alice", "email": "alice@example.com", "role": "admin" };
    printf("raw json : %s\n", alice.json());

    String label = f"User payload: {alice.json()}";
    printf("%s\n", label);

    /* can also concat */
    String envelope = "[" + alice.json() + "]";
    printf("array    : %s\n", envelope);

    /* 2. List<dict> — JSON store */
    List<dict>* store = new List<dict>();
    dict bob   = { "id": 2, "name": "Bob",   "email": "bob@example.com",   "role": "editor" };
    dict carol = { "id": 3, "name": "Carol", "email": "carol@example.com", "role": "viewer" };
    store->Add(alice);
    store->Add(bob);
    store->Add(carol);

    /* build a JSON array response using + since d.json() is now String */
    String arr = "[";
    for (int i = 0; i < store->Count(); i++) {
        if (i > 0) arr = arr + ",";
        arr = arr + store->Get(i).json();
    }
    arr = arr + "]";
    printf("store    : %s\n\n", arr);

    /* 3. path parsing */
    String path = "  /API/Users/42  ";
    String norm  = path.trim().lower();
    String pfx   = "/api/users/";
    String id_s  = norm.substr((int)pfx.length(), (int)norm.length() - (int)pfx.length());
    int uid = atoi(id_s);
    printf("path     : %s  →  id=%d\n", norm, uid);
    printf("routing  : starts_with=%d  ends_with=%d\n",
           norm.starts_with("/api/users"), norm.ends_with(id_s));

    /* 4. query string */
    String qs = "page=2&limit=5&sort=name&role=admin";
    printf("page=%s  limit=%s  sort=%s  role=%s\n",
           qparam(qs,"page"), qparam(qs,"limit"),
           qparam(qs,"sort"), qparam(qs,"role"));

    /* 5. method normalization */
    String method = "  post  ";
    printf("method   : [%s]\n", method.trim().upper());

    /* 6. response builder: status line + JSON body */
    int status = 201;
    dict created = { "id": 4, "name": "Dave", "email": "dave@example.com", "role": "viewer" };
    printf("\nHTTP/1.1 %d Created\n", status);
    printf("Content-Type: application/json\n\n%s\n", created.json());

    /* 7. f-string composing path + method + status — all String */
    String log_line = f"[201] POST /api/users → {created.json()}";
    printf("\nlog: %s\n", log_line);

    delete store;
    return 0;
}
