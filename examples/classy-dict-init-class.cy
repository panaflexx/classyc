class myC {
	int seq;

	myC() {
		this.seq = 5;
		printf("ctor seq=%d\n", this.seq);
	}
	~myC() { printf("deleted myC\n"); }

	int get_seq() {
		printf("Current seq=%d\n", this.seq);
		return this->seq;
	}

	void doit() {
		dict ev = { "seq": this.get_seq(), "type": "event", "event": "" };
		dict ev2 = { "seq": get_seq(), "type": "event", "event": "" };

		String s = ev.json;
		printf(f"ev = {s}\n");
	}
};

int main()
{
	myC c;
	//defer delete c;
	c.doit();

	return 0;
}
