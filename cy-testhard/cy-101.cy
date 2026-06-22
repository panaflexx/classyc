/* Test 101: Class with complex inheritance and virtual-like dispatch */
#include <stdio.h>

class Animal {
    String name;
    Animal(String n) { this.name = n; }
    void speak() { printf("%s makes a sound\n", this.name); }
    void move() { printf("%s moves\n", this.name); }
};

class Dog : Animal {
    Dog(String n) : Animal(n) {}
    void speak() { printf("%s barks\n", this.name); }
    void fetch() { printf("%s fetches\n", this.name); }
};

class Cat : Animal {
    Cat(String n) : Animal(n) {}
    void speak() { printf("%s meows\n", this.name); }
    void purr() { printf("%s purrs\n", this.name); }
};

void makeSpeak(Animal* a) {
    a->speak();
}

int main() {
    Dog* d = new Dog("Rex");
    Cat* c = new Cat("Whiskers");

    makeSpeak(d);
    makeSpeak(c);

    d->fetch();
    c->purr();

    // Test through base pointer
    Animal* animals[] = { d, c };
    for (auto a in animals) a->speak();

    return 0;
}
