class myClass {
	int find;
	int set(int v) {
		printf("myClass set v=%d\n", v);
		this.find = v;
		return v;
	}
};

class setClass {
	String name;
	int count;
	String arr[];	
};

void main() {
	myClass c;
	c.find = 10;
	printf("before set: c.find = %d\n", c.find);
	c.set(42);
	printf("after set: c.find = %d\n", c.find);
	
	// C-style designated initializer (requires TM_CLASS fix)
	setClass s = {
		.name = "hello",
		.count = 4,
		.arr = {"This", "is", "a", "test"}
	};
	printf("s.name = %s, s.count = %d\n", s.name, s.count);

	// Positional initializer (also requires TM_CLASS fix)
	// setClass s2 = {"world", 2};

	// Future: heap allocation with new keyword
	// setClass *sp = new setClass() { .name = "heap", .count = 1 };
}
