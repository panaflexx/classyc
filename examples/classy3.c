String myutf8 = "!!! Schöne Grüße 😊 !!!";
String pliny[3] = {"cats", "dogs", "bummer"};

class myClass {
	int find;
	int setvalue(int y) {
		printf("myClass set\n");
	}
};

void doit(int i) {
	}

int main() {
	auto x = 5;

	doit(x);

	myClass test;
	test.find = 10;
	test.setvalue(x);

	printf("Hello %d %s\n", x, myutf8 + pliny[0]);
	//printf("Hello %s\n", bob + myutf8);
	return 0;
}
