/* Test 131: Recursive data structures with generics */
#include <stdio.h>

class Node<T> {
    T value;
    Node<T>* left;
    Node<T>* right;

    Node(T v) { this.value = v; this.left = NULL; this.right = NULL; }

    void insert(T v) {
        if (v < this.value) {
            if (this.left == NULL) this.left = new Node<T>(v);
            else this.left->insert(v);
        } else {
            if (this.right == NULL) this.right = new Node<T>(v);
            else this.right->insert(v);
        }
    }

    void inorder() {
        if (this.left) this.left->inorder();
        printf(f"{this.value} ");
        if (this.right) this.right->inorder();
    }

    int count() {
        int c = 1;
        if (this.left) c += this.left->count();
        if (this.right) c += this.right->count();
        return c;
    }
};

int main() {
    // Int BST
    Node<int>* root = new Node<int>(50);
    int vals[] = {30, 70, 20, 40, 60, 80, 10, 25, 35, 45};
    for (auto v in vals) root->insert(v);
    printf("BST inorder: "); root->inorder(); printf("\n");
    printf("BST count: %d\n", root->count());

    // String BST
    Node<String>* strRoot = new Node<String>("mango");
    String fruits[] = {"apple", "banana", "cherry", "date", "elderberry", "fig", "grape"};
    for (auto f in fruits) strRoot->insert(f);
    printf("String BST inorder: "); strRoot->inorder(); printf("\n");

    return 0;
}
