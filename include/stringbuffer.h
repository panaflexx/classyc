class Buffer {
    char *data;      // Pointer to stack-allocated buffer (via alloca)
    size_t size;     // Current used size of the buffer
    size_t maxlen;   // Maximum capacity (including reserved byte)

public:
    // Constructor: Allocates buffer with alloca and copies len bytes from src
    Buffer(char *src, size_t len, size_t maxlen) {
        this->size = 0;
        this->maxlen = 0;
        this->data = 0;

        // Cap maxlen to prevent stack overflow (arbitrary limit of 4096 bytes)
        if (maxlen > 4096) {
            this->data = (char *)alloca(1);
            this->maxlen = 1;
            this->data[0] = '\0';
            return;
        }

        this->maxlen = maxlen;
        if (len > this->maxlen - 1) {
            len = this->maxlen - 1;  // Reserve 1 byte for null terminator
        }

        this->data = (char *)alloca(this->maxlen);
        __builtin_memset(this->data, 0, this->maxlen);  // Zero out buffer for safety

        if (src && len > 0) {
            __builtin_memcpy(this->data, src, len);
            this->size = len;
            this->data[this->size] = '\0';  // Ensure null termination
        }
    }

    // Destructor: Stack-based, no cleanup needed
    ~Buffer() {}

    // Append raw bytes to the buffer
    void append(char *other, size_t len) {
        if (!other || len == 0 || this->size >= this->maxlen - 1) {
            return;  // Nothing to append or no space left
        }

        if (len > this->maxlen - 1 - this->size) {
            len = this->maxlen - 1 - this->size;  // Truncate to fit
        }

        __builtin_memcpy(this->data + this->size, other, len);
        this->size += len;
        this->data[this->size] = '\0';  // Maintain null termination
    }

    // Clear the buffer
    void clear() {
        if (this->size > 0) {
            __builtin_memset(this->data, 0, this->size);
            this->size = 0;
            this->data[0] = '\0';  // Ensure null termination
        }
    }

    // Get the raw buffer
    char* get_data() {
        return this->data;
    }

    // Get the current size
    size_t get_size() {
        return this->size;
    }
};
