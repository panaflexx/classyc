/* Test 111: Any<> type erasure with complex scenarios */
#include <stdio.h>

interface Serializable {
    String serialize();
};

class Data impl Serializable {
    int id;
    String name;
    Data(int i, String n) { this.id = i; this.name = n; }
    String serialize() { return f"Data({this.id},{this.name})"; }
};

class Container {
    List<Any<Serializable>*> items;

    Container() { this.items = new List<Any<Serializable>*>(); }

    void add(Any<Serializable>* item) { this.items->Add(item); }

    void serializeAll() {
        for (auto item in this.items) {
            printf("%s\n", item->serialize());
        }
    }
};

int main() {
    Container* c = new Container();
    c->add(any<Serializable>(new Data(1, "first")));
    c->add(any<Serializable>(new Data(2, "second")));
    c->add(any<Serializable>(new Data(3, "third")));

    c->serializeAll();

    // Mixed with null
    List<Any<Serializable>*> mixed = {
        any<Serializable>(new Data(10, "ten")),
        (Any<Serializable>*)NULL,
        any<Serializable>(new Data(20, "twenty"))
    };

    for (auto m in mixed) {
        if (m != NULL) printf("mixed: %s\n", m->serialize());
        else printf("mixed: null\n");
    }

    return 0;
}
