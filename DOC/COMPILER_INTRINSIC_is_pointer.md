# Compiler Intrinsic: `is_pointer<T>`

## Purpose

Enable generic collections to distinguish pointer types from value types at compile time, allowing ownership-aware destructors.

## Use Case

```c
class List<T> {
    int _owns_ptrs;  // flag set by .owns() constructor variant
    
    List() { this->_owns_ptrs = 0; }  // default: non-owning
    List.owns() { this->_owns_ptrs = 1; }  // owns pointers
    
    ~List() {
        for (int i = 0; i < this->length; i++) {
            if (this->_owns_ptrs && is_pointer<T>()) {
                delete this->data[i];  // only compiled for T* specializations
            } else {
                __destroy(this->data[i]);  // existing behavior
            }
        }
        free(this->data);
    }
};

// Usage:
List<Track*>* lib = new List<Track*>.owns();  // owns Track* objects
lib->Add(new Track(...));
defer delete lib;  // deletes list AND all Track* objects (no manual loop!)

List<Track>* byval = new List<Track>();  // owns by-value copies
defer delete byval;  // __destroy runs ~Track() on each (already works)
```

## Implementation in `src/classyc.c`

### 1. Add intrinsic check (in `check()` function, around line 14665)

```c
/* is_pointer<T>: compiler intrinsic that returns 1 if T is a pointer type,
   0 otherwise. Used by generic collection destructors to conditionally
   delete pointer elements when the collection owns them.
   
   Syntax: is_pointer<TypeArg>()
   Returns: compile-time constant int (1 or 0)
   
   Rewritten during check phase to an integer literal based on the resolved
   type parameter T. */
if (op1->code == N_ID && strcmp(op1->u.s.s, "is_pointer") == 0) {
    /* Expect: is_pointer<T>() where T is a type parameter */
    node_t type_args = NL_NEXT(op1);  /* <T> part */
    node_t call_args = NL_NEXT(type_args);  /* () part */
    
    if (type_args != NULL && type_args->code == N_TYPE_ARGS 
        && call_args != NULL && NL_HEAD(call_args->u.ops) == NULL) {
        /* Extract the single type argument */
        node_t type_arg = NL_HEAD(type_args->u.ops);
        if (type_arg != NULL && NL_NEXT(type_arg) == NULL) {
            /* Resolve the type */
            struct type *t = check_type(c2m_ctx, type_arg, TRUE);
            int is_ptr = (t != NULL && t->mode == TM_PTR) ? 1 : 0;
            
            /* Replace the entire call with an integer literal */
            node_t lit = new_i_node(c2m_ctx, N_I, (long)is_ptr, POS(r));
            lit->attr = create_expr(c2m_ctx, lit);
            lit->attr->type = create_type(c2m_ctx, NULL);
            lit->attr->type->mode = TM_BASIC;
            lit->attr->type->u.basic_type = TP_INT;
            lit->attr->const_p = TRUE;
            lit->attr->const_addr.uns_p = FALSE;
            lit->attr->const_addr.u.i_val = is_ptr;
            
            /* Replace r with lit in the parent */
            *r = *lit;
            e = r->attr;
            break;
        }
    }
    /* If malformed, fall through to regular call handling (will error) */
}

/* __destroy(x) intrinsic (existing code follows)... */
if (op1->code == N_ID && strcmp(op1->u.s.s, "__destroy") == 0
    ...
```

### 2. Add test case in `cy-validate/`

```c
/* cy-validate/val-016-is-pointer.cy */
#include <stdio.h>

int main() {
    /* Test is_pointer<T> intrinsic */
    int p1 = is_pointer<int>();           // 0
    int p2 = is_pointer<int*>();          // 1
    int p3 = is_pointer<char*>();         // 1
    int p4 = is_pointer<void*>();         // 1
    int p5 = is_pointer<String>();        // 0 (String is char*, but as param it's value)
    
    printf("is_pointer<int>()   = %d (expect 0)\n", p1);
    printf("is_pointer<int*>()  = %d (expect 1)\n", p2);
    printf("is_pointer<char*>() = %d (expect 1)\n", p3);
    printf("is_pointer<void*>() = %d (expect 1)\n", p4);
    printf("is_pointer<String>()= %d (expect 0)\n", p5);
    
    int passed = (p1 == 0 && p2 == 1 && p3 == 1 && p4 == 1 && p5 == 0);
    printf("\n%s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
```

### 3. Update `include/list.h` to use the intrinsic

```c
class List<T> {
    T*  data;
    int length;
    int capacity;
    int _owns_ptrs;  // ownership flag
    
    List() {
        this->length = 0;
        this->capacity = 4;
        this->data = (T*) malloc(sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;  // default: non-owning
    }
    
    /* Owning constructor: List<T*>.owns() for pointer collections */
    List.owns() {
        this->length = 0;
        this->capacity = 4;
        this->data = (T*) malloc(sizeof(T) * this->capacity);
        this->_owns_ptrs = 1;  // owns pointer elements
    }
    
    /* Other constructors also need _owns_ptrs = 0... */
    
    ~List() {
        for (int i = 0; i < this->length; i++) {
            /* is_pointer<T>() resolves to 0 or 1 at compile time.
             * When T is not a pointer, the `if` is dead code elimination.
             * When T is a pointer, the branch compiles. */
            if (this->_owns_ptrs && is_pointer<T>()) {
                delete this->data[i];  // delete owned pointers
            } else {
                __destroy(this->data[i]);  // by-value elements
            }
        }
        if (this->data) free((void*) this->data);
    }
    
    /* ... rest of methods ... */
};
```

## Benefits

1. **Eliminates manual cleanup loops** for pointer collections:
   ```c
   // OLD: manual loop required
   for (auto t in library) delete t;
   defer delete library;
   
   // NEW: automatic cleanup
   List<Track*>* library = new List<Track*>.owns();
   defer delete library;  // one line!
   ```

2. **Keeps existing by-value semantics** working unchanged:
   ```c
   List<Track>* byval = new List<Track>();  // still works
   defer delete byval;  // __destroy still runs ~Track()
   ```

3. **Explicit ownership** via `.owns()` constructor makes intent clear

4. **Type-safe** - only compiles for pointer specializations when ownership flag is set

5. **Consistent with existing `__destroy` pattern** - both are compile-time type introspection

## Alternative: Use `_Generic` in C11

If you don't want to modify the compiler, you could use C11 `_Generic`:

```c
#define is_pointer(T) _Generic((T){0}, \
    int*: 1, char*: 1, void*: 1, \
    default: 0)
```

But this requires exhaustive listing of pointer types and doesn't work for generic template parameters.

## Recommendation

Implement `is_pointer<T>` as a compiler intrinsic following the same pattern as `__destroy`. It's a small, clean addition that solves a real usability problem.
