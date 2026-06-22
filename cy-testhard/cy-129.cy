/* Test 129: Complex struct/class layout and memory operations */
#include <stdio.h>
#include <string.h>

class Header {
    int magic;
    int version;
    int payload_size;
};

class Packet {
    Header header;
    char data[256];

    Packet(int magic, int version, String payload) {
        this.header.magic = magic;
        this.header.version = version;
        this.header.payload_size = payload.length();
        memcpy(this.data, payload, payload.length());
    }

    String getPayload() { return String(this.data, this.header.payload_size); }
    int getMagic() { return this.header.magic; }
};

class Buffer {
    char* data;
    int size;
    int capacity;

    Buffer(int cap) { this.capacity = cap; this.data = malloc(cap); this.size = 0; }
    ~Buffer() { free(this.data); }

    void write(char* src, int len) {
        if (this.size + len > this.capacity) return;
        memcpy(this.data + this.size, src, len);
        this.size += len;
    }

    String readString(int len) {
        if (len > this.size) len = this.size;
        String s = String(this.data, len);
        memmove(this.data, this.data + len, this.size -len);
        this.size -= len;
        return s;
    }
};

int main() {
    Packet* pkt = new Packet(0xDEADBEEF, 1, "Hello, ClassyC!");
    printf("packet: magic=0x%x ver=%d size=%d payload='%s'\n",
           pkt->getMagic(), pkt->header.version, pkt->header.payload_size, pkt->getPayload());
    delete pkt;

    Buffer* buf = new Buffer(1024);
    buf->write("first", 5);
    buf->write("second", 6);
    printf("buffer read: %s\n", buf->readString(5));
    printf("buffer read: %s\n", buf->readString(6));
    delete buf;

    // Memcpy between class instances
    Packet p1(0xCAFE, 2, "original");
    Packet p2 = p1;  // bitwise copy
    printf("copied packet: %s\n", p2.getPayload());

    return 0;
}
