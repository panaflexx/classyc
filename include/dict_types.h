/* dict_types.h — Public DictType tag for dict value inspection
 *
 * Include THIS header (not dict.h) from user-facing .cy code that needs to
 * branch on a dict value's runtime type — e.g. a typed bind() overload in a
 * database wrapper.
 *
 * Use the `d.type()` builtin to read the tag, then unwrap with an ordinary
 * cast:
 *
 *     switch (v.type()) {
 *       case DICT_STRING: use (char*)v;   break;
 *       case DICT_INT64:  use (long)v;    break;
 *       case DICT_NUMBER: use (double)v;  break;
 *       ...
 *     }
 *
 * NOTE: you cannot cast a `dict` to a `DictValue*` and read its fields — every
 * dict->pointer cast in .cy code unwraps to the union payload (the scalar at
 * offset 8), so `((DictValue*)v)->type` would read the payload bytes, not the
 * tag.  That is exactly why `d.type()` exists: it reads offset 0 (the tag)
 * without unwrapping.  The full DictValue struct lives in dict.h and is for the
 * runtime/compiler only.
 *
 * Keep these values identical to the DictType enum in dict.h.
 */

#ifndef CLASSYC_DICT_TYPES_H
#define CLASSYC_DICT_TYPES_H

typedef enum {
    DICT_NULL   = 0,
    DICT_BOOL   = 1,
    DICT_NUMBER = 2,
    DICT_INT64  = 3,
    DICT_STRING = 4,
    DICT_ARRAY  = 5,
    DICT_OBJECT = 6
} DictType;

#endif /* CLASSYC_DICT_TYPES_H */
