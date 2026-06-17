String myutf8 = "!!! Schöne Grüße 😊 !!!";
String pliny[3] = {"cats", "dogs", "bummer"};

class myClass {
	class myClass *this;
	int find;
	int length; 
	String hello;

	int go() {
		printf("go! %p\n", this);
		return -1;
	}
};


char ss[] = "char string array";

int main() {
	String search = "Hello, this is a test";

	printf("%s \n", search);

	myClass snatch;
	snatch.find = -10;
	snatch.hello = "Snarf snarf!";

	snatch.go();
	printf("snatch find = [%d] hello = \"%s\"\n", snatch.find, snatch.hello);

	printf("Hello %s\n", myutf8 + pliny[0]);
	return 0;
}
