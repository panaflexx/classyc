/* Copyright 2025 Roger Davenport */
/* MIT Licensed */

#ifndef DICT_H
#define DICT_H

#ifndef C2M_DICT_API
#define C2M_DICT_API static
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <inttypes.h> // for PRId64

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_KEY_LEN   (64 * 1024) // 8 KB max key length (bytes)
#define MAX_VALUE_LEN 20000000  // 20 million chars max string value length (characters)

/* ===== Stack / Arena allocator =====
 * If DictArena is provided to creation functions, allocations come from the
 * arena (bump allocator) and are freed only when the arena is reset/destroyed.
 * If arena is NULL, the functions fall back to heap malloc/realloc/free.
 */
typedef struct {
    char   *buf;     /* caller-owned buffer */
    size_t  size;    /* total bytes */
    size_t  used;    /* bytes consumed */
} DictArena;

C2M_DICT_API void dict_arena_init(DictArena *a, void *buffer, size_t size) {
    if (!a) return;
    a->buf = (char*)buffer;
    a->size = size;
    a->used = 0;
}

C2M_DICT_API void dict_arena_reset(DictArena *a) { if (a) a->used = 0; }
C2M_DICT_API size_t dict_arena_used(const DictArena *a) { return a ? a->used : 0; }

/* Internal allocator helper */
C2M_DICT_API void *dict_alloc(DictArena *arena, size_t sz) {
    if (!arena) return calloc(1, sz); /* zero-init so owned_arena starts NULL */
    if (arena->used + sz > arena->size) return NULL; /* OOM in arena */
    void *p = arena->buf + arena->used;
    arena->used += sz;
    return p;
}

/* Arena-aware strdup */
C2M_DICT_API char *dict_strdup_arena(DictArena *arena, const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = (char*)dict_alloc(arena, len);
    if (dst) memcpy(dst, src, len);
    return dst;
}

typedef enum {
    DICT_NULL,
    DICT_BOOL,
    DICT_NUMBER,
    DICT_INT64,
    DICT_STRING,
    DICT_ARRAY,
    DICT_OBJECT
} DictType;

typedef struct DictValue DictValue;

typedef struct {
    size_t length;
    DictValue **items;
} DictArray;

typedef struct {
    char *key;
    DictValue *value;
} DictKeyValuePair;

typedef struct {
    size_t count;
    size_t capacity;
    DictKeyValuePair *pairs;
} DictObject;

struct DictValue {
    DictType type;
    union {
        int bool_value;
        double number_value;
        int64_t int64_value;
        char *string_value;
        DictArray array_value;
        DictObject object_value;
    };
    /* Non-NULL only on the *root* object of a heap-arena-backed dict.
       Points to the DictArena header that was co-allocated with its buffer
       in a single malloc().  dict_destroy() uses this to free everything in
       one shot instead of a recursive walk. */
    DictArena *owned_arena;
};

C2M_DICT_API char *dict_strdup(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char *dst = (char *)malloc(len + 1);
    if (dst) memcpy(dst, src, len + 1);
    return dst;
}

C2M_DICT_API void dict_value_free(DictValue *val);

C2M_DICT_API void dict_object_init(DictObject *obj) {
    if (!obj) return;
    obj->count = 0;
    obj->capacity = 4;
    obj->pairs = (DictKeyValuePair *)malloc(sizeof(DictKeyValuePair) * obj->capacity);
    if (!obj->pairs) {
        obj->capacity = 0;
    }
}

C2M_DICT_API void dict_object_clear(DictObject *obj) {
    if (!obj) return;
    for (size_t i = 0; i < obj->count; i++) {
        free(obj->pairs[i].key);
        dict_value_free(obj->pairs[i].value);
        free(obj->pairs[i].value);
    }
    free(obj->pairs);
    obj->pairs = NULL;
    obj->count = 0;
    obj->capacity = 0;
}

C2M_DICT_API int dict_object_ensure_capacity(DictObject *obj, size_t new_capacity) {
    if (new_capacity <= obj->capacity)
        return 1; // already enough

    size_t new_cap;
    if (obj->capacity > (SIZE_MAX / 2)) {
        new_cap = new_capacity;
    } else {
        new_cap = obj->capacity * 2;
        if (new_cap < new_capacity)
            new_cap = new_capacity;
    }

    if (new_cap > SIZE_MAX / sizeof(DictKeyValuePair)) {
        return 0;
    }

    DictKeyValuePair *new_pairs = (DictKeyValuePair *)realloc(obj->pairs, new_cap * sizeof(DictKeyValuePair));
    if (!new_pairs)
        return 0; // failure

    obj->pairs = new_pairs;
    obj->capacity = new_cap;
    return 1;
}

/* ===== Arena-aware creation (internal) ===== */
C2M_DICT_API DictValue *dict_create_object_arena(DictArena *arena);
C2M_DICT_API DictValue *dict_create_null_arena(DictArena *arena);
C2M_DICT_API DictValue *dict_create_bool_arena(DictArena *arena, int b);
C2M_DICT_API DictValue *dict_create_number_arena(DictArena *arena, double n);
C2M_DICT_API DictValue *dict_create_int64_arena(DictArena *arena, int64_t n);
C2M_DICT_API DictValue *dict_create_string_arena(DictArena *arena, const char *s);
C2M_DICT_API DictValue *dict_create_array_arena(DictArena *arena);

/* ===== Public (heap) wrappers ===== */
C2M_DICT_API DictValue *dict_create_object() { return dict_create_object_arena(NULL); }
C2M_DICT_API DictValue *dict_create_null()   { return dict_create_null_arena(NULL); }
C2M_DICT_API DictValue *dict_create_bool(int b){ return dict_create_bool_arena(NULL, b); }
C2M_DICT_API DictValue *dict_create_number(double n){ return dict_create_number_arena(NULL, n); }
C2M_DICT_API DictValue *dict_create_int64(int64_t n){ return dict_create_int64_arena(NULL, n); }
C2M_DICT_API DictValue *dict_create_string(const char *s){ return dict_create_string_arena(NULL, s); }
C2M_DICT_API DictValue *dict_create_array(){ return dict_create_array_arena(NULL); }

/* ===== Arena implementations ===== */
C2M_DICT_API DictValue *dict_create_object_arena(DictArena *arena) {
    DictValue *val = (DictValue *)dict_alloc(arena, sizeof(DictValue));
    if (!val) return NULL;
    val->type = DICT_OBJECT;
    dict_object_init(&val->object_value);
    if (!val->object_value.pairs && val->object_value.capacity == 0) {
        if (!arena) free(val);
        return NULL;
    }
    return val;
}

C2M_DICT_API DictValue *dict_create_null_arena(DictArena *arena) {
    DictValue *val = (DictValue *)dict_alloc(arena, sizeof(DictValue));
    if (!val) return NULL;
    val->type = DICT_NULL;
    return val;
}

C2M_DICT_API DictValue *dict_create_bool_arena(DictArena *arena, int b) {
    DictValue *val = (DictValue *)dict_alloc(arena, sizeof(DictValue));
    if (!val) return NULL;
    val->type = DICT_BOOL;
    val->bool_value = b ? 1 : 0;
    return val;
}

C2M_DICT_API DictValue *dict_create_number_arena(DictArena *arena, double n) {
    DictValue *val = (DictValue *)dict_alloc(arena, sizeof(DictValue));
    if (!val) return NULL;
    val->type = DICT_NUMBER;
    val->number_value = n;
    return val;
}

C2M_DICT_API DictValue *dict_create_int64_arena(DictArena *arena, int64_t n) {
    DictValue *val = (DictValue *)dict_alloc(arena, sizeof(DictValue));
    if (!val) return NULL;
    val->type = DICT_INT64;
    val->int64_value = n;
    return val;
}

C2M_DICT_API DictValue *dict_create_string_arena(DictArena *arena, const char *s) {
    if (!s) return NULL;
    size_t len = strnlen(s, MAX_VALUE_LEN);
    if (len == 0 || len >= MAX_VALUE_LEN) {
        return NULL;
    }

    DictValue *val = (DictValue *)dict_alloc(arena, sizeof(DictValue));
    if (!val) return NULL;
    val->type = DICT_STRING;
    val->string_value = dict_strdup_arena(arena, s);
    if (!val->string_value) {
        if (!arena) free(val);
        return NULL;
    }
    return val;
}

C2M_DICT_API DictValue *dict_create_array_arena(DictArena *arena) {
    DictValue *val = (DictValue *)dict_alloc(arena, sizeof(DictValue));
    if (!val) return NULL;
    val->type = DICT_ARRAY;
    val->array_value.length = 0;
    val->array_value.items = NULL;
    return val;
}

/* Heap wrappers (defined via arena functions above) */

/* Resize array items if needed */
C2M_DICT_API int dict_array_ensure_capacity(DictArray *arr, size_t new_capacity) {
    if (new_capacity <= arr->length)
        return 1;
    size_t new_cap = arr->length == 0 ? 4 : arr->length * 2;
    if (new_cap < new_capacity) new_cap = new_capacity;
    DictValue **new_items = (DictValue **)realloc(arr->items, new_cap * sizeof(DictValue *));
    if (!new_items) return 0;
    arr->items = new_items;
    return 1;
}

C2M_DICT_API int dict_array_append(DictValue *array_val, DictValue *new_val) {
    if (!array_val || array_val->type != DICT_ARRAY || !new_val) return 0;

    DictArray *array = &array_val->array_value;

    if (!dict_array_ensure_capacity(array, array->length + 1))
        return 0; // realloc failed

    array->items[array->length] = new_val;
    array->length++;

    return 1;
}

/* Free a DictValue recursively */
C2M_DICT_API void dict_value_free(DictValue *val) {
    if (!val) return;
    switch (val->type) {
        case DICT_STRING:
            free(val->string_value);
            break;
        case DICT_ARRAY:
            for (size_t i = 0; i < val->array_value.length; i++) {
                dict_value_free(val->array_value.items[i]);
                free(val->array_value.items[i]);
            }
            free(val->array_value.items);
            break;
        case DICT_OBJECT:
            dict_object_clear(&val->object_value);
            break;
        default:
            break;
    }
}

/* Find key index in object */
C2M_DICT_API size_t dict_object_find_key(const DictObject *obj, const char *key) {
    if (!obj || !key) return (size_t)-1;
    for (size_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->pairs[i].key, key) == 0) return i;
    }
    return (size_t)-1;
}

/* Set or insert a key-value pair */
C2M_DICT_API int dict_object_set(DictValue *obj_val, const char *key, DictValue *new_val) {
    if (!obj_val || obj_val->type != DICT_OBJECT || !key || !new_val) return 0;
    size_t key_len = strnlen(key, MAX_KEY_LEN);
    if (key_len == 0 || key_len >= MAX_KEY_LEN) return 0;

    DictObject *obj = &obj_val->object_value;
    size_t idx = dict_object_find_key(obj, key);
    if (idx != (size_t)-1) {
        /* Guard against storing a value back into the same slot it already
         * occupies (e.g. d["k"] = d["k"]).  Freeing first and then storing the
         * same pointer would leave a dangling value (use-after-free). */
        if (obj->pairs[idx].value == new_val)
            return 1;
        dict_value_free(obj->pairs[idx].value);
        free(obj->pairs[idx].value);
        obj->pairs[idx].value = new_val;
        return 1;
    }
    if (!dict_object_ensure_capacity(obj, obj->count + 1))
        return 0;

    char *key_copy = dict_strdup(key);
    if (!key_copy)
        return 0;

    obj->pairs[obj->count].key = key_copy;
    obj->pairs[obj->count].value = new_val;
    obj->count++;
    return 1;
}

/* Get value by key */
C2M_DICT_API DictValue *dict_object_get(const DictValue *obj_val, const char *key) {
    if (!obj_val || obj_val->type != DICT_OBJECT || !key) return NULL;
    const DictObject *obj = &obj_val->object_value;
    size_t idx = dict_object_find_key(obj, key);
    if (idx == (size_t)-1) return NULL;
    return obj->pairs[idx].value;
}

/* Remove key-value pair */
C2M_DICT_API int dict_object_remove(DictValue *obj_val, const char *key) {
    if (!obj_val || obj_val->type != DICT_OBJECT || !key) return 0;
    DictObject *obj = &obj_val->object_value;
    size_t idx = dict_object_find_key(obj, key);
    if (idx == (size_t)-1) return 0;
    free(obj->pairs[idx].key);
    dict_value_free(obj->pairs[idx].value);
    free(obj->pairs[idx].value);
    for (size_t i = idx; i + 1 < obj->count; i++) {
        obj->pairs[i] = obj->pairs[i + 1];
    }
    obj->count--;
    return 1;
}

C2M_DICT_API int dict_key_compare(const void *a, const void *b) {
    const DictKeyValuePair *pa = (const DictKeyValuePair *)a;
    const DictKeyValuePair *pb = (const DictKeyValuePair *)b;
    return strcmp(pa->key, pb->key);
}

C2M_DICT_API void dict_object_sort_keys(DictObject *obj) {
    if (!obj || obj->count < 2) return;
    qsort(obj->pairs, obj->count, sizeof(DictKeyValuePair), dict_key_compare);
}

/* JSON serialization helpers (unchanged) */
C2M_DICT_API int dict_append_to_buffer(char **cursor, size_t *remaining, const char *s, size_t len) {
    if (len >= *remaining) return 0;
    memcpy(*cursor, s, len);
    *cursor += len;
    *remaining -= len;
    return 1;
}

C2M_DICT_API int dict_append_char(char **cursor, size_t *remaining, char c) {
    if (*remaining < 1) return 0;
    **cursor = c;
    (*cursor)++;
    (*remaining)--;
    return 1;
}

C2M_DICT_API int dict_append_escaped_string(char **cursor, size_t *remaining, const char *s) {
    if (!dict_append_char(cursor, remaining, '"')) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            if (!dict_append_char(cursor, remaining, '\\')) return 0;
            if (!dict_append_char(cursor, remaining, c)) return 0;
        } else if (c == '\b') {
            if (!dict_append_to_buffer(cursor, remaining, "\\b", 2)) return 0;
        } else if (c == '\f') {
            if (!dict_append_to_buffer(cursor, remaining, "\\f", 2)) return 0;
        } else if (c == '\n') {
            if (!dict_append_to_buffer(cursor, remaining, "\\n", 2)) return 0;
        } else if (c == '\r') {
            if (!dict_append_to_buffer(cursor, remaining, "\\r", 2)) return 0;
        } else if (c == '\t') {
            if (!dict_append_to_buffer(cursor, remaining, "\\t", 2)) return 0;
        } else if (c < 0x20) {
            char esc[7];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            if (!dict_append_to_buffer(cursor, remaining, esc, 6)) return 0;
        } else {
            if (!dict_append_char(cursor, remaining, c)) return 0;
        }
    }
    if (!dict_append_char(cursor, remaining, '"')) return 0;
    return 1;
}

C2M_DICT_API int dict_serialize_value_pretty(const DictValue *val, char **cursor, size_t *remaining,
                                        int indent_level, int pretty) {
    if (!val) return dict_append_to_buffer(cursor, remaining, "null", 4);

    const char *indent_str = pretty ? "  " : "";
    #define APPEND_NEWLINE_AND_INDENT() do { \
        if (pretty) { \
            if (!dict_append_char(cursor, remaining, '\n')) return 0; \
            for (int _i = 0; _i < indent_level; _i++) \
                if (!dict_append_to_buffer(cursor, remaining, indent_str, 2)) return 0; \
        } \
    } while (0)

    switch (val->type) {
    case DICT_NULL:
        return dict_append_to_buffer(cursor, remaining, "null", 4);
    case DICT_BOOL:
        return dict_append_to_buffer(cursor, remaining,
                                     val->bool_value ? "true" : "false",
                                     val->bool_value ? 4 : 5);
    case DICT_NUMBER: {
        char n[64];
        int len = snprintf(n, sizeof(n), "%.17g", val->number_value);
        return dict_append_to_buffer(cursor, remaining, n, len);
    }
    case DICT_INT64: {
        char n[32];
        int len = snprintf(n, sizeof(n), "%" PRId64, val->int64_value);
        return dict_append_to_buffer(cursor, remaining, n, len);
    }
    case DICT_STRING:
        return dict_append_escaped_string(cursor, remaining, val->string_value);
    case DICT_ARRAY: {
        if (!dict_append_char(cursor, remaining, '[')) return 0;
        int n = val->array_value.length;
        for (int i = 0; i < n; i++) {
            if (i > 0) {
                if (!dict_append_char(cursor, remaining, ',')) return 0;
            }
            APPEND_NEWLINE_AND_INDENT();
            if (!dict_serialize_value_pretty(val->array_value.items[i], cursor, remaining,
                                             indent_level + 1, pretty)) return 0;
        }
        if (pretty && n > 0) {
            if (!dict_append_char(cursor, remaining, '\n')) return 0;
            for (int _i = 0; _i < indent_level; _i++)
                if (!dict_append_to_buffer(cursor, remaining, indent_str, 2)) return 0;
        }
        if (!dict_append_char(cursor, remaining, ']')) return 0;
        return 1;
    }
    case DICT_OBJECT: {
        if (!dict_append_char(cursor, remaining, '{')) return 0;
        size_t count = val->object_value.count;
        DictKeyValuePair *pairs = val->object_value.pairs;
        for (size_t i = 0; i < count; i++) {
            if (i > 0) {
                if (!dict_append_char(cursor, remaining, ',')) return 0;
            }
            APPEND_NEWLINE_AND_INDENT();
            if (!dict_append_escaped_string(cursor, remaining, pairs[i].key)) return 0;
            if (!dict_append_to_buffer(cursor, remaining, pretty ? ": " : ":", 1 + (pretty ? 1 : 0))) return 0;
            if (!dict_serialize_value_pretty(pairs[i].value, cursor, remaining,
                                             indent_level + 1, pretty)) return 0;
        }
        if (pretty && count > 0) {
            if (!dict_append_char(cursor, remaining, '\n')) return 0;
            for (int _i = 0; _i < indent_level; _i++)
                if (!dict_append_to_buffer(cursor, remaining, indent_str, 2)) return 0;
        }
        if (!dict_append_char(cursor, remaining, '}')) return 0;
        return 1;
    }
    default:
        return 0;
    }
    #undef APPEND_NEWLINE_AND_INDENT
}

C2M_DICT_API char *dict_serialize_json(const DictValue *val, char *buffer, size_t buf_len, int pretty) {
    if (!buffer || buf_len == 0) return NULL;

    char *cursor = buffer;
    size_t remaining = buf_len;

    if (!dict_serialize_value_pretty(val, &cursor, &remaining, 0, pretty)) {
        if (buf_len > 0) buffer[0] = '\0';
        return NULL;
    }

    if (remaining == 0) {
        if (buf_len > 0) buffer[0] = '\0';
        return NULL;
    }
    *cursor = '\0';
    return buffer;
}

/* Path lookup */
C2M_DICT_API DictValue *dict_find_path(const DictValue *root, const char *path) {
    if (!root || root->type != DICT_OBJECT || !path || *path == '\0') return NULL;

    const DictValue *current = root;
    const char *segment_start = path;
    while (segment_start) {
        const char *slash = strchr(segment_start, '/');
        size_t len = slash ? (size_t)(slash - segment_start) : strlen(segment_start);

        if (len == 0) return NULL;

        const DictObject *obj = &current->object_value;
        DictValue *next_val = NULL;
        for (size_t i = 0; i < obj->count; i++) {
            if (strncmp(obj->pairs[i].key, segment_start, len) == 0 && obj->pairs[i].key[len] == '\0') {
                next_val = obj->pairs[i].value;
                break;
            }
        }
        if (!next_val) return NULL;

        if (!slash) {
            return (DictValue *)next_val;
        }

        if (next_val->type != DICT_OBJECT) return NULL;

        current = next_val;
        segment_start = slash + 1;
    }

    return NULL;
}

/* Destroy entire DictValue */
/* Iteration helpers for for-in loops */
C2M_DICT_API size_t dict_object_count(const DictValue *obj) {
    if (!obj || obj->type != DICT_OBJECT) return 0;
    return obj->object_value.count;
}
C2M_DICT_API const char *dict_object_key_at(const DictValue *obj, size_t index) {
    if (!obj || obj->type != DICT_OBJECT || index >= obj->object_value.count) return NULL;
    return obj->object_value.pairs[index].key;
}
C2M_DICT_API DictValue *dict_object_value_at(const DictValue *obj, size_t index) {
    if (!obj || obj->type != DICT_OBJECT || index >= obj->object_value.count) return NULL;
    return obj->object_value.pairs[index].value;
}

C2M_DICT_API void dict_destroy(DictValue *val) {
    if (!val) return;
    DictArena *arena = val->owned_arena;
    /* Always free the heap-allocated contents (keys, pair arrays, nested
       values) regardless of whether an arena backs the root. */
    val->owned_arena = NULL; /* prevent re-entry */
    dict_value_free(val);
    if (arena) {
        /* Arena-backed root: the DictArena header and its buffer were
           co-allocated in a single malloc() by dict_create_heap_arena().
           One free() reclaims everything. */
        free(arena);
    } else {
        /* Standard path: root DictValue was individually malloc'd. */
        free(val);
    }
}

/* Create a dict whose root DictValue is backed by a heap-owned arena.
 *
 * The DictArena header and its buffer are co-allocated in one malloc() call:
 *   [ DictArena header ][ arena buffer (bytes) ]
 *
 * The root DictValue is bump-allocated from the start of the arena buffer.
 * Keys, nested values, and pair arrays added via dict_object_set() still use
 * individual malloc() calls (the full arena path is future work), so
 * dict_destroy() still recurses to free those — but the single free(arena)
 * at the end reclaims the arena block rather than free(root_val).
 *
 * Typical use:
 *   dict d = dict_create_heap_arena(256 * 1024);
 *   defer_delete(d);           // dict_destroy(d) frees everything cleanly
 */
C2M_DICT_API DictValue *dict_create_heap_arena(size_t bytes) {
    if (bytes == 0) bytes = 256 * 1024; /* 256 KB default */
    /* Single allocation: [DictArena header][arena buffer] */
    size_t total = sizeof(DictArena) + bytes;
    char *block = (char *)malloc(total);
    if (!block) return NULL;
    DictArena *a = (DictArena *)block;
    a->buf  = block + sizeof(DictArena);
    a->size = bytes;
    a->used = 0;
    DictValue *root = dict_create_object_arena(a);
    if (!root) { free(block); return NULL; }
    root->owned_arena = a;
    return root;
}

/* Deep-copy a DictValue onto the heap (recursive).  Returns a fully
 * independent clone that the caller owns, or NULL on failure / NULL input.
 *
 * This is used when a dict value is stored into another dict slot (e.g.
 * d2.k = d1.j, or d["a"] = d["b"]).  Without an owned copy the destination
 * would alias the source's DictValue*, which leads to a use-after-free on
 * self-assignment and a double-free when both dicts are destroyed. */
C2M_DICT_API DictValue *dict_value_copy(const DictValue *src) {
    if (!src) return NULL;
    switch (src->type) {
        case DICT_NULL:   return dict_create_null();
        case DICT_BOOL:   return dict_create_bool(src->bool_value);
        case DICT_NUMBER: return dict_create_number(src->number_value);
        case DICT_INT64:  return dict_create_int64(src->int64_value);
        case DICT_STRING: return dict_create_string(src->string_value);
        case DICT_ARRAY: {
            DictValue *arr = dict_create_array();
            if (!arr) return NULL;
            for (size_t i = 0; i < src->array_value.length; i++) {
                DictValue *item = dict_value_copy(src->array_value.items[i]);
                if (!item || !dict_array_append(arr, item)) {
                    dict_destroy(item);
                    dict_destroy(arr);
                    return NULL;
                }
            }
            return arr;
        }
        case DICT_OBJECT: {
            DictValue *obj = dict_create_object();
            if (!obj) return NULL;
            for (size_t i = 0; i < src->object_value.count; i++) {
                DictValue *child = dict_value_copy(src->object_value.pairs[i].value);
                if (!child || !dict_object_set(obj, src->object_value.pairs[i].key, child)) {
                    dict_destroy(child);
                    dict_destroy(obj);
                    return NULL;
                }
            }
            return obj;
        }
    }
    return NULL;
}

/* JSON parser (unchanged from original for brevity, but arena variants could be added similarly) */
typedef struct {
    const char *buffer;
    size_t buffer_len;
    size_t pos;
    char *error_str;
    size_t error_str_len;
} DictJsonParser;

C2M_DICT_API DictValue *dict_parse_value(DictJsonParser *p);

C2M_DICT_API void dict_parser_error(DictJsonParser *p, const char *msg) {
    if (p->error_str && p->error_str_len > 0) {
        strncpy(p->error_str, msg, p->error_str_len - 1);
        p->error_str[p->error_str_len - 1] = '\0';
    }
}

C2M_DICT_API int dict_parser_peek(DictJsonParser *p) {
    if (p->pos >= p->buffer_len) return 0;
    return (unsigned char)p->buffer[p->pos];
}

C2M_DICT_API int dict_parser_consume(DictJsonParser *p, char expected) {
    if (dict_parser_peek(p) != expected) return 0;
    p->pos++;
    return 1;
}

C2M_DICT_API void dict_parser_skip_whitespace(DictJsonParser *p) {
    while (p->pos < p->buffer_len) {
        char c = p->buffer[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

C2M_DICT_API int dict_parse_literal(DictJsonParser *p, const char *lit) {
    size_t len = strlen(lit);
    if (p->pos + len > p->buffer_len) return 0;
    if (strncmp(p->buffer + p->pos, lit, len) != 0) return 0;
    p->pos += len;
    return 1;
}

C2M_DICT_API char *dict_parse_json_string(DictJsonParser *p) {
    if (!dict_parser_consume(p, '"')) {
        dict_parser_error(p, "Expected '\"' for string");
        return NULL;
    }

    size_t out_capacity = 64;
    size_t out_length = 0;
    char *out = (char *)malloc(out_capacity);
    if (!out) {
        dict_parser_error(p, "Out of memory");
        return NULL;
    }

    while (p->pos < p->buffer_len) {
        char c = p->buffer[p->pos++];
        if (c == '"') {
            if (out_length + 1 > out_capacity) {
                char *tmp = (char *)realloc(out, out_length + 1);
                if (!tmp) { free(out); return NULL; }
                out = tmp;
            }
            out[out_length] = '\0';
            return out;
        }
        if (c == '\\') {
            if (p->pos >= p->buffer_len) { free(out); return NULL; }
            char esc = p->buffer[p->pos++];
            char decoded;
            if (esc == '"' || esc == '\\' || esc == '/') decoded = esc;
            else if (esc == 'b') decoded = '\b';
            else if (esc == 'f') decoded = '\f';
            else if (esc == 'n') decoded = '\n';
            else if (esc == 'r') decoded = '\r';
            else if (esc == 't') decoded = '\t';
            else if (esc == 'u') {
                unsigned int codepoint = 0;
                for (int i = 0; i < 4; i++) {
                    if (p->pos >= p->buffer_len) { free(out); return NULL; }
                    char h = p->buffer[p->pos++];
                    unsigned int val;
                    if (h >= '0' && h <= '9') val = h - '0';
                    else if (h >= 'a' && h <= 'f') val = 10 + h - 'a';
                    else if (h >= 'A' && h <= 'F') val = 10 + h - 'A';
                    else { free(out); return NULL; }
                    codepoint = (codepoint << 4) | val;
                }
                if (codepoint <= 0x7F) {
                    decoded = (char)codepoint;
                } else if (codepoint <= 0x7FF) {
                    if (out_length + 2 > out_capacity) {
                        size_t new_capacity = out_capacity * 2;
                        char *tmp = (char *)realloc(out, new_capacity);
                        if (!tmp) { free(out); return NULL; }
                        out = tmp; out_capacity = new_capacity;
                    }
                    out[out_length++] = (char)(0xC0 | (codepoint >> 6));
                    out[out_length++] = (char)(0x80 | (codepoint & 0x3F));
                    continue;
                } else {
                    if (out_length + 3 > out_capacity) {
                        size_t new_capacity = out_capacity * 2;
                        char *tmp = (char *)realloc(out, new_capacity);
                        if (!tmp) { free(out); return NULL; }
                        out = tmp; out_capacity = new_capacity;
                    }
                    out[out_length++] = (char)(0xE0 | (codepoint >> 12));
                    out[out_length++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                    out[out_length++] = (char)(0x80 | (codepoint & 0x3F));
                    continue;
                }
            } else { free(out); return NULL; }
            if (out_length + 1 > out_capacity) {
                size_t new_capacity = out_capacity * 2;
                char *tmp = (char *)realloc(out, new_capacity);
                if (!tmp) { free(out); return NULL; }
                out = tmp; out_capacity = new_capacity;
            }
            out[out_length++] = decoded;
        } else {
            if (out_length + 1 > out_capacity) {
                size_t new_capacity = out_capacity * 2;
                char *tmp = (char *)realloc(out, new_capacity);
                if (!tmp) { free(out); return NULL; }
                out = tmp; out_capacity = new_capacity;
            }
            out[out_length++] = c;
        }
    }
    free(out);
    return NULL;
}

C2M_DICT_API int dict_parse_number(DictJsonParser *p, double *out) {
    size_t start_pos = p->pos;
    if (dict_parser_peek(p) == '-') p->pos++;
    while (p->pos < p->buffer_len) {
        char c = dict_parser_peek(p);
        if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
            p->pos++;
        else
            break;
    }
    size_t num_len = p->pos - start_pos;
    if (num_len == 0) return 0;

    char *endptr;
    char num_buf[128];
    if (num_len >= sizeof(num_buf)) return 0;
    memcpy(num_buf, p->buffer + start_pos, num_len);
    num_buf[num_len] = '\0';

    double val = strtod(num_buf, &endptr);
    if (*endptr != '\0') return 0;
    *out = val;
    return 1;
}

C2M_DICT_API DictValue *dict_parse_array(DictJsonParser *p);
C2M_DICT_API DictValue *dict_parse_object(DictJsonParser *p);

C2M_DICT_API DictValue *dict_parse_value(DictJsonParser *p) {
    dict_parser_skip_whitespace(p);
    if (p->pos >= p->buffer_len) {
        dict_parser_error(p, "Unexpected end of input");
        return NULL;
    }
    char c = dict_parser_peek(p);
    if (c == '"') {
        char *str = dict_parse_json_string(p);
        if (!str) return NULL;
        DictValue *val = dict_create_string(str);
        free(str);
        return val;
    } else if (c == '{') {
        return dict_parse_object(p);
    } else if (c == '[') {
        return dict_parse_array(p);
    } else if ((c == '-') || (c >= '0' && c <= '9')) {
        double num;
        if (!dict_parse_number(p, &num)) return NULL;
        /* Use DICT_INT64 when the value is an exact integer (no fractional part)
           so that ClassyC dict field reads work correctly via the i64 union slot. */
        if (num >= (double)INT64_MIN && num <= (double)INT64_MAX && num == (double)(int64_t)num)
            return dict_create_int64((int64_t)num);
        return dict_create_number(num);
    } else if (dict_parse_literal(p, "true")) {
        return dict_create_bool(1);
    } else if (dict_parse_literal(p, "false")) {
        return dict_create_bool(0);
    } else if (dict_parse_literal(p, "null")) {
        return dict_create_null();
    } else {
        dict_parser_error(p, "Unexpected token");
        return NULL;
    }
}

C2M_DICT_API DictValue *dict_parse_array(DictJsonParser *p) {
    if (!dict_parser_consume(p, '[')) {
        dict_parser_error(p, "Expected '[' for array");
        return NULL;
    }
    dict_parser_skip_whitespace(p);

    DictValue *array = dict_create_array();
    if (!array) {
        dict_parser_error(p, "Out of memory creating array");
        return NULL;
    }

    size_t capacity = 4;
    size_t length = 0;
    DictValue **items = (DictValue **)malloc(sizeof(DictValue *) * capacity);
    if (!items) {
        dict_destroy(array);
        dict_parser_error(p, "Out of memory");
        return NULL;
    }

    if (dict_parser_peek(p) == ']') {
        p->pos++;
        array->array_value.length = 0;
        array->array_value.items = NULL;
        free(items);
        return array;
    }

    for (;;) {
        DictValue *item = dict_parse_value(p);
        if (!item) {
            for (size_t i = 0; i < length; i++) dict_destroy(items[i]);
            free(items);
            dict_destroy(array);
            return NULL;
        }
        if (length + 1 > capacity) {
            size_t new_capacity = capacity * 2;
            DictValue **tmp = (DictValue **)realloc(items, sizeof(DictValue *) * new_capacity);
            if (!tmp) {
                dict_destroy(item);
                for (size_t i = 0; i < length; i++) dict_destroy(items[i]);
                free(items);
                dict_destroy(array);
                return NULL;
            }
            items = tmp;
            capacity = new_capacity;
        }
        items[length++] = item;

        dict_parser_skip_whitespace(p);
        if (dict_parser_consume(p, ',')) {
            dict_parser_skip_whitespace(p);
            continue;
        } else if (dict_parser_consume(p, ']')) {
            break;
        } else {
            dict_parser_error(p, "Expected ',' or ']' in array");
            for (size_t i = 0; i < length; i++) dict_destroy(items[i]);
            free(items);
            dict_destroy(array);
            return NULL;
        }
    }

    array->array_value.length = length;
    array->array_value.items = items;
    return array;
}

C2M_DICT_API DictValue *dict_parse_object(DictJsonParser *p) {
    if (!dict_parser_consume(p, '{')) {
        dict_parser_error(p, "Expected '{' for object");
        return NULL;
    }
    dict_parser_skip_whitespace(p);

    DictValue *obj = dict_create_object();
    if (!obj) {
        dict_parser_error(p, "Out of memory creating object");
        return NULL;
    }

    if (dict_parser_peek(p) == '}') {
        p->pos++;
        return obj;
    }

    for (;;) {
        dict_parser_skip_whitespace(p);
        char *key = dict_parse_json_string(p);
        if (!key) {
            dict_destroy(obj);
            return NULL;
        }
        dict_parser_skip_whitespace(p);
        if (!dict_parser_consume(p, ':')) {
            dict_parser_error(p, "Expected ':' after key");
            free(key);
            dict_destroy(obj);
            return NULL;
        }
        DictValue *value = dict_parse_value(p);
        if (!value) {
            free(key);
            dict_destroy(obj);
            return NULL;
        }
        if (!dict_object_set(obj, key, value)) {
            free(key);
            dict_destroy(value);
            dict_destroy(obj);
            return NULL;
        }
        free(key);

        dict_parser_skip_whitespace(p);
        if (dict_parser_consume(p, ',')) continue;
        else if (dict_parser_consume(p, '}')) break;
        else {
            dict_parser_error(p, "Expected ',' or '}' in object");
            dict_destroy(obj);
            return NULL;
        }
    }
    return obj;
}

C2M_DICT_API DictValue *dict_deserialize_json(const char *json, char *err_buf, size_t err_len) {
    DictJsonParser parser = {json, strlen(json), 0, err_buf, err_len};
    DictValue *result = dict_parse_value(&parser);
    dict_parser_skip_whitespace(&parser);
    if (parser.pos != parser.buffer_len) {
        dict_parser_error(&parser, "Trailing garbage after JSON value");
        dict_destroy(result);
        return NULL;
    }
    return result;
}

/* BSON support (unchanged for brevity; arena variants can follow the same pattern) */
#define BSON_TYPE_DOUBLE    0x01
#define BSON_TYPE_STRING    0x02
#define BSON_TYPE_DOCUMENT  0x03
#define BSON_TYPE_ARRAY     0x04
#define BSON_TYPE_BOOL      0x08
#define BSON_TYPE_NULL      0x0A
#define BSON_TYPE_INT64     0x12

C2M_DICT_API void bson_write_int32_le(uint8_t *buf, int32_t v) { memcpy(buf, &v, 4); }
C2M_DICT_API int32_t bson_read_int32_le(const uint8_t *buf) { int32_t v; memcpy(&v, buf, 4); return v; }
C2M_DICT_API void bson_write_int64_le(uint8_t *buf, int64_t v) { memcpy(buf, &v, 8); }
C2M_DICT_API int64_t bson_read_int64_le(const uint8_t *buf) { int64_t v; memcpy(&v, buf, 8); return v; }

C2M_DICT_API int bson_serialize_document(const DictValue *doc, uint8_t *buf, size_t buf_len);

C2M_DICT_API int bson_serialize_value(uint8_t *buf, size_t buf_len, size_t *written,
                                 const char *key, const DictValue *val);

C2M_DICT_API int bson_serialize_document(const DictValue *doc, uint8_t *buf, size_t buf_len) {
    if (!doc || (doc->type != DICT_OBJECT && doc->type != DICT_ARRAY)) return 0;
    size_t pos = 4;
    size_t count = (doc->type == DICT_OBJECT) ? doc->object_value.count : doc->array_value.length;

    for (size_t i = 0; i < count; i++) {
        uint8_t type_byte;
        const char *key;
        const DictValue *item_val;
        if (doc->type == DICT_OBJECT) {
            key = doc->object_value.pairs[i].key;
            item_val = doc->object_value.pairs[i].value;
        } else {
            static char idxbuf[32];
            snprintf(idxbuf, sizeof(idxbuf), "%zu", i);
            key = idxbuf;
            item_val = doc->array_value.items[i];
        }
        if (!bson_serialize_value(buf + pos, buf_len - pos, &pos, key, item_val)) return 0;
    }
    if (pos + 1 > buf_len) return 0;
    buf[pos++] = 0x00;
    int32_t total_len = (int32_t)pos;
    bson_write_int32_le(buf, total_len);
    return (int)pos;
}

C2M_DICT_API int bson_serialize_value(uint8_t *buf, size_t buf_len, size_t *written,
                                 const char *key, const DictValue *val) {
    size_t pos = 0;
    size_t key_len = strlen(key);
    if (pos + 1 + key_len + 1 > buf_len) return 0;

    uint8_t type_byte;
    switch (val->type) {
    case DICT_NUMBER: type_byte = BSON_TYPE_DOUBLE; break;
    case DICT_STRING: type_byte = BSON_TYPE_STRING; break;
    case DICT_OBJECT: type_byte = BSON_TYPE_DOCUMENT; break;
    case DICT_ARRAY:  type_byte = BSON_TYPE_ARRAY; break;
    case DICT_BOOL:   type_byte = BSON_TYPE_BOOL; break;
    case DICT_NULL:   type_byte = BSON_TYPE_NULL; break;
    case DICT_INT64:  type_byte = BSON_TYPE_INT64; break;
    default: return 0;
    }
    buf[pos++] = type_byte;
    memcpy(buf + pos, key, key_len + 1); pos += key_len + 1;

    switch (val->type) {
    case DICT_NUMBER: {
        double d = val->number_value;
        if (pos + 8 > buf_len) return 0;
        memcpy(buf + pos, &d, 8); pos += 8;
        break;
    }
    case DICT_STRING: {
        const char *s = val->string_value;
        size_t str_len = strlen(s);
        if (pos + 4 + str_len + 1 > buf_len) return 0;
        int32_t sl = (int32_t)(str_len + 1);
        bson_write_int32_le(buf + pos, sl); pos += 4;
        memcpy(buf + pos, s, str_len + 1); pos += str_len + 1;
        break;
    }
    case DICT_OBJECT:
    case DICT_ARRAY: {
        size_t val_written = bson_serialize_document(val, buf + pos, buf_len - pos);
        if (val_written == 0) return 0;
        pos += val_written;
        break;
    }
    case DICT_BOOL:
        if (pos + 1 > buf_len) return 0;
        buf[pos++] = val->bool_value ? 1 : 0;
        break;
    case DICT_NULL:
        break;
    case DICT_INT64:
        if (pos + 8 > buf_len) return 0;
        bson_write_int64_le(buf + pos, val->int64_value); pos += 8;
        break;
    default: return 0;
    }
    *written = pos;
    return 1;
}

C2M_DICT_API DictValue *bson_deserialize_document_internal(const uint8_t *buf, size_t buf_len,
                                                      size_t *read_bytes, int is_array);

C2M_DICT_API size_t dict_serialize_bson(const DictValue *val, uint8_t *buf, size_t buf_len) {
    return bson_serialize_document(val, buf, buf_len);
}

C2M_DICT_API DictValue *dict_deserialize_bson(const uint8_t *buf, size_t buf_len, size_t *read_bytes) {
    return bson_deserialize_document_internal(buf, buf_len, read_bytes, 0);
}

C2M_DICT_API DictValue *bson_deserialize_document_internal(const uint8_t *buf, size_t buf_len,
                                                      size_t *read_bytes, int is_array) {
    if (!buf || buf_len < 5 || !read_bytes) return NULL;

    int32_t doc_len = bson_read_int32_le(buf);
    if (doc_len < 5 || (size_t)doc_len > buf_len) return NULL;

    size_t pos = 4;
    DictValue *result = is_array ? dict_create_array() : dict_create_object();
    if (!result) return NULL;

    while (pos < (size_t)doc_len - 1) {
        uint8_t type_byte = buf[pos++];
        size_t key_start = pos;
        while (pos < (size_t)doc_len && buf[pos] != 0) pos++;
        if (pos >= (size_t)doc_len) goto fail;
        const char *key_ptr = (const char *)(buf + key_start);
        pos++; /* skip null */

        DictValue *val = NULL;
        size_t val_read = 0;
        switch (type_byte) {
        case BSON_TYPE_DOUBLE: {
            if (pos + 8 > (size_t)doc_len) goto fail;
            union { double d; uint8_t b[8]; } u;
            for (int i = 0; i < 8; i++) u.b[i] = buf[pos + i];
            val = dict_create_number(u.d);
            val_read = 8;
            break;
        }
        case BSON_TYPE_INT64:
            if (pos + 8 > (size_t)doc_len) goto fail;
            val = dict_create_int64(bson_read_int64_le(buf + pos));
            val_read = 8;
            break;
        case BSON_TYPE_STRING: {
            if (pos + 4 > (size_t)doc_len) goto fail;
            int32_t str_len = bson_read_int32_le(buf + pos);
            if (str_len < 1 || pos + 4 + (size_t)str_len > (size_t)doc_len) goto fail;
            char *str = (char *)malloc(str_len);
            if (!str) goto fail;
            memcpy(str, buf + pos + 4, str_len);
            str[str_len-1] = 0;
            val = dict_create_string(str);
            free(str);
            val_read = 4 + (size_t)str_len;
            break;
        }
        case BSON_TYPE_DOCUMENT:
        case BSON_TYPE_ARRAY:
            val = bson_deserialize_document_internal(buf + pos, buf_len - pos, &val_read,
                                                     type_byte == BSON_TYPE_ARRAY);
            if (!val) goto fail;
            break;
        case BSON_TYPE_BOOL:
            if (pos + 1 > (size_t)doc_len) goto fail;
            val = dict_create_bool(buf[pos] != 0);
            val_read = 1;
            break;
        case BSON_TYPE_NULL:
            val = dict_create_null();
            val_read = 0;
            break;
        default:
            goto fail;
        }
        pos += val_read;

        char *key_copy = dict_strdup(key_ptr);
        if (!key_copy) { dict_destroy(val); goto fail; }

        if (is_array) {
            dict_array_append(result, val);
        } else {
            dict_object_set(result, key_copy, val);
        }
        free(key_copy);
    }
    if (read_bytes) *read_bytes = doc_len;
    return result;

fail:
    dict_destroy(result);
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* DICT_H */
