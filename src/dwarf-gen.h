/* dwarf-gen.h — Minimal DWARF4 debug section generator for b2obj.
   Produces .debug_info, .debug_abbrev, .debug_line, .debug_str
   from MIR debug info tables (mir-dbinfo.h).

   Designed for: GDB single-stepping and `print` of variables.
   DWARF version 4 (most widely supported). */

#ifndef DWARF_GEN_H
#define DWARF_GEN_H

#include <stdint.h>
#include <stddef.h>

/* A simple growable byte buffer. */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} dwbuf_t;

static void dwbuf_init(dwbuf_t *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void dwbuf_free(dwbuf_t *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }
static void dwbuf_grow(dwbuf_t *b, size_t need) {
    if (b->len + need <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 256;
    while (nc < b->len + need) nc *= 2;
    b->data = realloc(b->data, nc);
    b->cap = nc;
}
static void dwbuf_u8(dwbuf_t *b, uint8_t v) {
    dwbuf_grow(b, 1); b->data[b->len++] = v;
}
static void dwbuf_u16(dwbuf_t *b, uint16_t v) {
    dwbuf_grow(b, 2); memcpy(b->data + b->len, &v, 2); b->len += 2;
}
static void dwbuf_u32(dwbuf_t *b, uint32_t v) {
    dwbuf_grow(b, 4); memcpy(b->data + b->len, &v, 4); b->len += 4;
}
static void dwbuf_u64(dwbuf_t *b, uint64_t v) {
    dwbuf_grow(b, 8); memcpy(b->data + b->len, &v, 8); b->len += 8;
}
static void dwbuf_bytes(dwbuf_t *b, const void *p, size_t n) {
    dwbuf_grow(b, n); memcpy(b->data + b->len, p, n); b->len += n;
}
/* DWARF unsigned LEB128 */
static void dwbuf_uleb(dwbuf_t *b, uint64_t v) {
    do {
        uint8_t byte = v & 0x7f;
        v >>= 7;
        if (v) byte |= 0x80;
        dwbuf_u8(b, byte);
    } while (v);
}
/* DWARF signed LEB128 */
static void dwbuf_sleb(dwbuf_t *b, int64_t v) {
    int more = 1;
    while (more) {
        uint8_t byte = v & 0x7f;
        v >>= 7;
        if ((v == 0 && !(byte & 0x40)) || (v == -1 && (byte & 0x40)))
            more = 0;
        else
            byte |= 0x80;
        dwbuf_u8(b, byte);
    }
}
/* DWARF string (null-terminated in buffer) */
static void dwbuf_str(dwbuf_t *b, const char *s) {
    size_t n = strlen(s) + 1;
    dwbuf_bytes(b, s, n);
}
/* Patch a u32 at a given offset */
static void dwbuf_patch_u32(dwbuf_t *b, size_t off, uint32_t v) {
    memcpy(b->data + off, &v, 4);
}

/* ---- DWARF4 constants ---- */
/* Tags */
#define DW_TAG_compile_unit     0x11
#define DW_TAG_subprogram       0x2e
#define DW_TAG_formal_parameter 0x05
#define DW_TAG_variable         0x34
#define DW_TAG_base_type        0x24
#define DW_TAG_pointer_type     0x0f
#define DW_TAG_structure_type   0x13
#define DW_TAG_member           0x0d
#define DW_TAG_array_type       0x01
#define DW_TAG_subrange_type    0x21
#define DW_TAG_enumeration_type 0x04
#define DW_TAG_enumerator       0x28
#define DW_TAG_typedef          0x16
#define DW_TAG_const_type       0x26
#define DW_TAG_volatile_type    0x35
#define DW_TAG_union_type       0x17
#define DW_TAG_subroutine_type  0x15
#define DW_TAG_unspecified_parameters 0x18
#define DW_TAG_restrict_type    0x37
#define DW_TAG_unspecified_type 0x3b

/* Attributes */
#define DW_AT_name              0x03
#define DW_AT_stmt_list         0x10
#define DW_AT_low_pc            0x11
#define DW_AT_high_pc           0x12
#define DW_AT_language          0x13
#define DW_AT_comp_dir          0x1b
#define DW_AT_producer          0x25
#define DW_AT_byte_size         0x0b
#define DW_AT_bit_size          0x0d
#define DW_AT_bit_offset        0x0c  /* DWARF4 style */
#define DW_AT_encoding          0x3e
#define DW_AT_type              0x49
#define DW_AT_data_member_location 0x38
#define DW_AT_decl_file         0x3a
#define DW_AT_decl_line         0x3b
#define DW_AT_location          0x02
#define DW_AT_frame_base        0x40
#define DW_AT_external          0x3f
#define DW_AT_const_value       0x1c
#define DW_AT_count             0x37
#define DW_AT_upper_bound       0x2f

/* Forms */
#define DW_FORM_addr            0x01
#define DW_FORM_data1           0x0b
#define DW_FORM_data2           0x05
#define DW_FORM_data4           0x06
#define DW_FORM_data8           0x07
#define DW_FORM_string          0x08
#define DW_FORM_sec_offset      0x17
#define DW_FORM_ref4            0x13
#define DW_FORM_flag_present    0x19
#define DW_FORM_exprloc         0x18
#define DW_FORM_udata           0x0f
#define DW_FORM_sdata           0x0d

/* Children flag */
#define DW_CHILDREN_yes 1
#define DW_CHILDREN_no  0

/* Languages */
#define DW_LANG_C11 0x001d

/* Location ops */
#define DW_OP_fbreg  0x91
#define DW_OP_addr   0x03
#define DW_OP_reg0   0x50  /* DW_OP_reg0..reg31 */
#define DW_OP_breg0  0x70  /* DW_OP_breg0..breg31: base reg + SLEB offset */
#define DW_OP_regx   0x90  /* ULEB reg number */
#define DW_OP_bregx  0x92  /* ULEB reg number + SLEB offset */
#define DW_OP_deref  0x06
#define DW_OP_plus_uconst 0x23  /* ULEB constant added to top of stack */
#define DW_OP_call_frame_cfa 0x9c

/* Line number standard opcodes */
#define DW_LNS_copy            1
#define DW_LNS_advance_pc      2
#define DW_LNS_advance_line    3
#define DW_LNS_set_file        4
#define DW_LNS_set_column      5
#define DW_LNS_negate_stmt     6
#define DW_LNS_const_add_pc    8
#define DW_LNS_fixed_advance_pc 9

/* Extended opcodes */
#define DW_LNE_end_sequence    1
#define DW_LNE_set_address     2
#define DW_LNE_define_file     3

/* Base-type encodings */
#define DW_ATE_address      0x01
#define DW_ATE_boolean      0x02
#define DW_ATE_float        0x04
#define DW_ATE_signed       0x05
#define DW_ATE_signed_char  0x06
#define DW_ATE_unsigned     0x07
#define DW_ATE_unsigned_char 0x08
#define DW_ATE_UTF          0x10

#endif /* DWARF_GEN_H */
