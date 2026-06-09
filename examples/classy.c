#include <inc/exception.h>

//String myString = (String)"classy string\0";
//string newString = myString;

String myutf8 = "!!! Schöne Grüße 😊 !!!";
//const char blah[] = " - animal";
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
	Exception e;
	String search = "Hello, this is a test";

	printf("%s \n", search);

	Try {
		myClass snatch;
		snatch.find = -10;
		snatch.hello = "Snarf snarf!";
		//snatch.this = &snatch;
		//snatch.this.find = 10;

		//snatch.go();
		printf("snatch find = [%d] hello = \"%s\"\n", snatch.find, snatch.hello);
	
		Throw(-1, "Bad pie");
	} Catch(e) {
		printf("Exception: %s\n", e.msg);
	}

	printf("Hello %s\n", myutf8 + pliny[0]);
	return 0;
}
