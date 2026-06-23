/* test-list-byval.cy — by-value class elements in List<T> with element dtors */
#include <stdio.h>
#include "list.h"

int track_dtor_count = 0;

class Track {
    int id;
    Track(int id) { this->id = id; }
    ~Track() { track_dtor_count = track_dtor_count + 1; }
    int getId() { return this->id; }
};

int main() {
    List<Track>* lst = new List<Track>();

    Track a = Track(1);
    Track b = Track(2);
    Track c = Track(3);
    lst->Add(a);
    lst->Add(b);
    lst->Add(c);

    printf("count: %d\n", lst->Count());

    int sum = 0;
    for (auto t in lst) sum = sum + t.getId();
    printf("sum: %d\n", sum);

    /* The list's three element destructors should fire on delete. */
    int before = track_dtor_count;
    delete lst;
    int list_dtors = track_dtor_count - before;
    printf("element dtors on delete: %d\n", list_dtors);

    if (list_dtors != 3) {
        printf("FAIL: expected 3 element dtors, got %d\n", list_dtors);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
