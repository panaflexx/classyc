/* classy9.cy — mini List ToJson smoke test
 * @expect: pass
 */
#include <stdio.h>
#include "list.h"

int main() {
	auto blah = List<int>();
	blah.Add(1);
	auto sah = List<String>();
	sah.Add("sah");

	auto d = {"animal": "cat"};
	printf(f"{d.json()} - {blah.ToJson()} - {sah.ToJson()}\n");
	return 0;
}
