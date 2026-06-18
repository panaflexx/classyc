//String myString = (String)"classy string\0";
String myutf8 = "!!! Schöne Grüße 😊 !!!";
String pliny[3] = {"cats", "dogs", "bummer"};

class myClass {
	int find;
	int length; 
	String hello;

	int go() {
		printf("this ptr access: %d\n", this->find);
		return -1;
	}
};

class declareClass {
	int find=10;
	int length=42;
};

char ss[] = "char string array";

void main() {
	String search = "Hello, this is a test";

	printf("%s \n", search);

	myClass snatch;
	snatch.find = -10;
	snatch.hello = "Snarf snarf!";

	snatch.go();
	printf("snatch find = [%d] hello = \"%s\"\n", snatch.find, snatch.hello);

	declareClass dc;
	printf("dc.find = %d, dc.length = %d\n", dc.find, dc.length);

	printf("Hello %s\n", myutf8 + pliny[0]);
}
