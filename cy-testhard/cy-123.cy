/* Test 123: Interface default methods and multiple inheritance */
#include <stdio.h>

interface Printable {
    void print();
    default void printTwice() { this.print(); this.print(); }
};

interface Serializable {
    String serialize();
    default String serializePretty() { return "pretty: " + this.serialize(); }
};

class Document impl Printable, Serializable {
    String content;
    Document(String c) { this.content = c; }

    void print() { printf("Document: %s\n", this.content); }
    String serialize() { return f"doc:{this.content}"; }
};

class Report impl Printable {
    String title;
    Report(String t) { this.title = t; }
    void print() { printf("Report: %s\n", this.title); }
};

void printAny(Printable* p) {
    p->print();
    p->printTwice();
}

int main() {
    Document* doc = new Document("Hello World");
    Report* rep = new Report("Quarterly");

    printAny(doc);
    printAny(rep);

    // Serializable methods
    printf("serialize: %s\n", doc->serialize());
    printf("serializePretty: %s\n", doc->serializePretty());

    // Multiple interface references
    Printable* p1 = doc;
    Serializable* s1 = doc;
    p1->print();
    printf("via Serializable: %s\n", s1->serialize());

    return 0;
}
