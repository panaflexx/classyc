#include <stdio.h>
#include <list.h>

int main() {
	List<int> blah  = {1};
	List<String> sah = {"sah"};

	auto d = {"animal": "cat"};
	printf(f"{d.json} - {blah.ToJson()} - {sah.ToJson()}\n");
}
