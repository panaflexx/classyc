/* dwarf-aot-emit.h — Shared DWARF4 + .debug_frame emission for b2obj / b2objmac.
   Input: MIR context (for module dbinfo) + per-function code/offsets.
   Output: section buffers + absolute-address reloc list (sect: 0=info,1=line,2=frame).
   Arch-neutral section bytes; callers map relocs to ELF RELA or Mach-O UNSIGNED. */

#ifndef DWARF_AOT_EMIT_H
#define DWARF_AOT_EMIT_H

#include <stdio.h>
#include <unistd.h>
#include "dwarf-gen.h"
#include "mir.h"

#if !MIR_NO_DBINFO

typedef struct {
  const char *name;
  const uint8_t *code;
  size_t code_len;
  size_t text_offset;
  MIR_func_t func; /* may be NULL if no dbinfo */
} dwarf_aot_func_t;

typedef struct {
  size_t offset;
  int64_t addend;
  int sect; /* 0=.debug_info, 1=.debug_line, 2=.debug_frame */
} dwarf_aot_reloc_t;

typedef struct {
  dwbuf_t info, abbrev, line, str, frame;
  dwarf_aot_reloc_t *relocs;
  size_t n_relocs;
} dwarf_aot_result_t;

static char dwarf_aot_name_buf[512];
static const char *dwarf_aot_display_name(const char *mir_name) {
  if (mir_name == NULL) return "?";
  if (strncmp(mir_name, "__ctor_", 7) == 0) {
    const char *cls = mir_name + 7;
    const char *sep = strstr(cls, "__");
    size_t clen = sep ? (size_t)(sep - cls) : strlen(cls);
    snprintf(dwarf_aot_name_buf, sizeof(dwarf_aot_name_buf), "%.*s::%.*s",
             (int)clen, cls, (int)clen, cls);
    return dwarf_aot_name_buf;
  }
  if (strncmp(mir_name, "__dtor_", 7) == 0) {
    const char *cls = mir_name + 7;
    const char *sep = strstr(cls, "__");
    size_t clen = sep ? (size_t)(sep - cls) : strlen(cls);
    snprintf(dwarf_aot_name_buf, sizeof(dwarf_aot_name_buf), "%.*s::~%.*s",
             (int)clen, cls, (int)clen, cls);
    return dwarf_aot_name_buf;
  }
  const char *dbl = strstr(mir_name, "__");
  if (dbl != NULL && dbl != mir_name) {
    const char *under = NULL;
    for (const char *p = mir_name; p < dbl; p++)
      if (*p == '_') under = p;
    if (under != NULL && under > mir_name && under < dbl - 1) {
      size_t clen = (size_t)(under - mir_name);
      size_t mlen = (size_t)(dbl - under - 1);
      snprintf(dwarf_aot_name_buf, sizeof(dwarf_aot_name_buf), "%.*s::%.*s",
               (int)clen, mir_name, (int)mlen, under + 1);
      return dwarf_aot_name_buf;
    }
  }
  return mir_name;
}

static void dwarf_aot_result_init(dwarf_aot_result_t *o) {
  memset(o, 0, sizeof(*o));
  dwbuf_init(&o->info);
  dwbuf_init(&o->abbrev);
  dwbuf_init(&o->line);
  dwbuf_init(&o->str);
  dwbuf_init(&o->frame);
}

static void dwarf_aot_result_free(dwarf_aot_result_t *o) {
  dwbuf_free(&o->info);
  dwbuf_free(&o->abbrev);
  dwbuf_free(&o->line);
  dwbuf_free(&o->str);
  dwbuf_free(&o->frame);
  free(o->relocs);
  o->relocs = NULL;
  o->n_relocs = 0;
}

/* Emit CFI always; full DIE/line when module has source_files (-g). */
static void dwarf_aot_emit(MIR_context_t ctx, size_t text_size,
                           const dwarf_aot_func_t *funcs, size_t n_funcs,
                           dwarf_aot_result_t *out) {
  dwarf_aot_result_init(out);
  size_t cap_rel = 0;
#define DA_RELOC(off, add, sect) do { \
    if (out->n_relocs >= cap_rel) { \
      cap_rel = cap_rel ? cap_rel * 2 : 32; \
      out->relocs = realloc(out->relocs, cap_rel * sizeof(dwarf_aot_reloc_t)); \
    } \
    out->relocs[out->n_relocs++] = (dwarf_aot_reloc_t){(off), (add), (sect)}; \
  } while (0)

  /* --- .debug_frame (always, for backtraces) --- */
  {
    dwarf_frame_func_t *ff = calloc(n_funcs ? n_funcs : 1, sizeof(dwarf_frame_func_t));
    dwarf_frame_reloc_t *fr = NULL;
    size_t nfr = 0;
    for (size_t i = 0; i < n_funcs; i++) {
      ff[i].code = funcs[i].code;
      ff[i].code_len = funcs[i].code_len;
      ff[i].text_offset = funcs[i].text_offset;
    }
    dwarf_emit_debug_frame(ff, n_funcs, &out->frame, &fr, &nfr);
    free(ff);
    for (size_t i = 0; i < nfr; i++)
      DA_RELOC(fr[i].offset, fr[i].addend, 2);
    free(fr);
  }

  MIR_module_t mod = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
  /* Full DIE/line only with -g (source_files). CFI already emitted above. */
  if (mod != NULL && mod->num_source_files > 0) {
  dwbuf_t *dw_abbrev = &out->abbrev;
  dwbuf_t *dw_info = &out->info;
  dwbuf_t *dw_line = &out->line;
  dwbuf_t *dw_str = &out->str;

  dwbuf_u8(dw_str, 0);
  dwbuf_str(dw_str, "classyc (MIR)");
  {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, ".");
    dwbuf_str(dw_str, cwd);
  }

  /* Abbrevs 1-20 (same as b2obj) */
  dwbuf_uleb(dw_abbrev, 1); dwbuf_uleb(dw_abbrev, DW_TAG_compile_unit); dwbuf_u8(dw_abbrev, DW_CHILDREN_yes);
  dwbuf_uleb(dw_abbrev, DW_AT_producer); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_language); dwbuf_uleb(dw_abbrev, DW_FORM_data2);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_comp_dir); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_low_pc); dwbuf_uleb(dw_abbrev, DW_FORM_addr);
  dwbuf_uleb(dw_abbrev, DW_AT_high_pc); dwbuf_uleb(dw_abbrev, DW_FORM_data8);
  dwbuf_uleb(dw_abbrev, DW_AT_stmt_list); dwbuf_uleb(dw_abbrev, DW_FORM_sec_offset);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 2); dwbuf_uleb(dw_abbrev, DW_TAG_subprogram); dwbuf_u8(dw_abbrev, DW_CHILDREN_yes);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_low_pc); dwbuf_uleb(dw_abbrev, DW_FORM_addr);
  dwbuf_uleb(dw_abbrev, DW_AT_high_pc); dwbuf_uleb(dw_abbrev, DW_FORM_data8);
  dwbuf_uleb(dw_abbrev, DW_AT_frame_base); dwbuf_uleb(dw_abbrev, DW_FORM_exprloc);
  dwbuf_uleb(dw_abbrev, DW_AT_decl_file); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, DW_AT_decl_line); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, DW_AT_external); dwbuf_uleb(dw_abbrev, DW_FORM_flag_present);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 3); dwbuf_uleb(dw_abbrev, DW_TAG_formal_parameter); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_decl_line); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, DW_AT_location); dwbuf_uleb(dw_abbrev, DW_FORM_exprloc);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 4); dwbuf_uleb(dw_abbrev, DW_TAG_variable); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_decl_line); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, DW_AT_location); dwbuf_uleb(dw_abbrev, DW_FORM_exprloc);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 5); dwbuf_uleb(dw_abbrev, DW_TAG_base_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_byte_size); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, DW_AT_encoding); dwbuf_uleb(dw_abbrev, DW_FORM_data1);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 6); dwbuf_uleb(dw_abbrev, DW_TAG_pointer_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_byte_size); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 7); dwbuf_uleb(dw_abbrev, DW_TAG_pointer_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_byte_size); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 8); dwbuf_uleb(dw_abbrev, DW_TAG_typedef); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 9); dwbuf_uleb(dw_abbrev, DW_TAG_const_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 10); dwbuf_uleb(dw_abbrev, DW_TAG_volatile_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 11); dwbuf_uleb(dw_abbrev, DW_TAG_restrict_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 12); dwbuf_uleb(dw_abbrev, DW_TAG_structure_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_yes);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_byte_size); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 13); dwbuf_uleb(dw_abbrev, DW_TAG_union_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_yes);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_byte_size); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 14); dwbuf_uleb(dw_abbrev, DW_TAG_member); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, DW_AT_data_member_location); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 15); dwbuf_uleb(dw_abbrev, DW_TAG_enumeration_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_yes);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_byte_size); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 16); dwbuf_uleb(dw_abbrev, DW_TAG_enumerator); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_name); dwbuf_uleb(dw_abbrev, DW_FORM_string);
  dwbuf_uleb(dw_abbrev, DW_AT_const_value); dwbuf_uleb(dw_abbrev, DW_FORM_sdata);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 17); dwbuf_uleb(dw_abbrev, DW_TAG_array_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_yes);
  dwbuf_uleb(dw_abbrev, DW_AT_type); dwbuf_uleb(dw_abbrev, DW_FORM_ref4);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 18); dwbuf_uleb(dw_abbrev, DW_TAG_subrange_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, DW_AT_upper_bound); dwbuf_uleb(dw_abbrev, DW_FORM_udata);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 19); dwbuf_uleb(dw_abbrev, DW_TAG_subrange_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);

  dwbuf_uleb(dw_abbrev, 20); dwbuf_uleb(dw_abbrev, DW_TAG_unspecified_type); dwbuf_u8(dw_abbrev, DW_CHILDREN_no);
  dwbuf_uleb(dw_abbrev, 0); dwbuf_uleb(dw_abbrev, 0);
  dwbuf_uleb(dw_abbrev, 0);

  size_t cu_start = dw_info->len;
  dwbuf_u32(dw_info, 0);
  dwbuf_u16(dw_info, 4);
  dwbuf_u32(dw_info, 0);
  dwbuf_u8(dw_info, 8);

  dwbuf_uleb(dw_info, 1);
  dwbuf_str(dw_info, "classyc (MIR)");
  dwbuf_u16(dw_info, DW_LANG_C11);
  {
    const char *cu_name = "<unknown>";
    for (uint32_t fi = 1; fi <= mod->num_source_files; fi++) {
      if (mod->source_files[fi] != NULL && mod->source_files[fi][0] != '<') {
        cu_name = mod->source_files[fi];
        break;
      }
    }
    dwbuf_str(dw_info, cu_name);
  }
  {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, ".");
    dwbuf_str(dw_info, cwd);
  }
  DA_RELOC(dw_info->len, 0, 0);
  dwbuf_u64(dw_info, 0);
  dwbuf_u64(dw_info, text_size);
  dwbuf_u32(dw_info, 0);

  MIR_dbtype_table_t *dbtypes = mod->dbtypes;
  uint32_t num_types = dbtypes != NULL ? dbtypes->num_types : 0;
  size_t *type_off = NULL;
  struct { size_t pos; uint32_t id; } *tfix = NULL;
  size_t n_tfix = 0, cap_tfix = 0;
#define TYPE_REF(TID) do { \
    if (n_tfix == cap_tfix) { cap_tfix = cap_tfix ? cap_tfix*2 : 64; \
      tfix = realloc(tfix, cap_tfix * sizeof(*tfix)); } \
    tfix[n_tfix].pos = dw_info->len; tfix[n_tfix].id = (uint32_t)(TID); n_tfix++; \
    dwbuf_u32(dw_info, 0); \
  } while (0)

  size_t void_off = dw_info->len - cu_start;
  dwbuf_uleb(dw_info, 20);
  if (num_types > 0) {
    type_off = malloc(num_types * sizeof(size_t));
    for (uint32_t i = 0; i < num_types; i++) type_off[i] = void_off;
    for (uint32_t id = 1; id < num_types; id++) {
      MIR_dbtype_t *t = &dbtypes->types[id];
      type_off[id] = dw_info->len - cu_start;
      switch (t->kind) {
      case MIR_DBT_BASE:
        dwbuf_uleb(dw_info, 5);
        dwbuf_str(dw_info, t->name ? t->name : "");
        dwbuf_uleb(dw_info, t->byte_size);
        dwbuf_u8(dw_info, (uint8_t)(t->u.base.encoding ? t->u.base.encoding : DW_ATE_signed));
        break;
      case MIR_DBT_PTR:
        if (t->u.ref.target_id == 0) {
          dwbuf_uleb(dw_info, 7);
          dwbuf_uleb(dw_info, t->byte_size ? t->byte_size : 8);
        } else {
          dwbuf_uleb(dw_info, 6);
          dwbuf_uleb(dw_info, t->byte_size ? t->byte_size : 8);
          TYPE_REF(t->u.ref.target_id);
        }
        break;
      case MIR_DBT_TYPEDEF:
        dwbuf_uleb(dw_info, 8);
        dwbuf_str(dw_info, t->name ? t->name : "");
        TYPE_REF(t->u.ref.target_id);
        break;
      case MIR_DBT_CONST:
        dwbuf_uleb(dw_info, 9); TYPE_REF(t->u.ref.target_id); break;
      case MIR_DBT_VOLATILE:
        dwbuf_uleb(dw_info, 10); TYPE_REF(t->u.ref.target_id); break;
      case MIR_DBT_RESTRICT:
        dwbuf_uleb(dw_info, 11); TYPE_REF(t->u.ref.target_id); break;
      case MIR_DBT_STRUCT:
      case MIR_DBT_UNION:
        dwbuf_uleb(dw_info, t->kind == MIR_DBT_STRUCT ? 12 : 13);
        dwbuf_str(dw_info, t->name ? t->name : "");
        dwbuf_uleb(dw_info, t->byte_size);
        for (uint32_t mi = 0; mi < t->u.aggregate.num_members; mi++) {
          MIR_dbmember_t *mb = &t->u.aggregate.members[mi];
          dwbuf_uleb(dw_info, 14);
          dwbuf_str(dw_info, mb->name ? mb->name : "");
          TYPE_REF(mb->type_id);
          dwbuf_uleb(dw_info, mb->byte_offset);
        }
        dwbuf_u8(dw_info, 0);
        break;
      case MIR_DBT_ENUM:
        dwbuf_uleb(dw_info, 15);
        dwbuf_str(dw_info, t->name ? t->name : "");
        dwbuf_uleb(dw_info, t->byte_size ? t->byte_size : 4);
        TYPE_REF(t->u.enumeration.underlying_id);
        for (uint32_t ei = 0; ei < t->u.enumeration.num_enumerators; ei++) {
          MIR_dbenumerator_t *en = &t->u.enumeration.enumerators[ei];
          dwbuf_uleb(dw_info, 16);
          dwbuf_str(dw_info, en->name ? en->name : "");
          dwbuf_sleb(dw_info, en->value);
        }
        dwbuf_u8(dw_info, 0);
        break;
      case MIR_DBT_ARRAY:
        dwbuf_uleb(dw_info, 17);
        TYPE_REF(t->u.array.element_id);
        if (t->u.array.count >= 0) {
          dwbuf_uleb(dw_info, 18);
          dwbuf_uleb(dw_info, (uint64_t)(t->u.array.count > 0 ? t->u.array.count - 1 : 0));
        } else {
          dwbuf_uleb(dw_info, 19);
        }
        dwbuf_u8(dw_info, 0);
        break;
      default:
        type_off[id] = void_off;
        break;
      }
    }
  }

  for (size_t fi = 0; fi < n_funcs; fi++) {
    const dwarf_aot_func_t *fe = &funcs[fi];
    MIR_func_t func = fe->func;
    dwbuf_uleb(dw_info, 2);
    dwbuf_str(dw_info, dwarf_aot_display_name(fe->name));
    DA_RELOC(dw_info->len, (int64_t)fe->text_offset, 0);
    dwbuf_u64(dw_info, 0);
    dwbuf_u64(dw_info, fe->code_len);
    dwbuf_uleb(dw_info, 1);
    dwbuf_u8(dw_info, DW_OP_call_frame_cfa);
    dwbuf_uleb(dw_info, 1);
    {
      uint32_t first_line = 0;
      if (func != NULL) {
        for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, func->insns);
             insn != NULL; insn = DLIST_NEXT(MIR_insn_t, insn)) {
          if (insn->source_line != 0) { first_line = insn->source_line; break; }
        }
      }
      dwbuf_uleb(dw_info, first_line);
    }
    if (func != NULL && func->dbinfo != NULL) {
      for (uint32_t vi = 0; vi < func->dbinfo->num_vars; vi++) {
        MIR_dbvar_t *v = &func->dbinfo->vars[vi];
        if (v->source_name == NULL) continue;
        dwbuf_uleb(dw_info, v->is_param ? 3 : 4);
        dwbuf_str(dw_info, v->source_name);
        dwbuf_uleb(dw_info, v->decl_line);
        TYPE_REF(v->type_id);
        dwbuf_t loc; dwbuf_init(&loc);
        if (v->mach_kind == MIR_DBMACH_MEM) {
          if (v->mach_reg < 32) dwbuf_u8(&loc, (uint8_t)(DW_OP_breg0 + v->mach_reg));
          else { dwbuf_u8(&loc, DW_OP_bregx); dwbuf_uleb(&loc, v->mach_reg); }
          dwbuf_sleb(&loc, v->mach_offset);
          if (v->mach_deref) {
            dwbuf_u8(&loc, DW_OP_deref);
            if (v->mach_offset2 != 0) {
              dwbuf_u8(&loc, DW_OP_plus_uconst);
              dwbuf_uleb(&loc, (uint64_t)(uint32_t)v->mach_offset2);
            }
          }
        } else if (v->mach_kind == MIR_DBMACH_REG) {
          if (v->mach_reg < 32) dwbuf_u8(&loc, (uint8_t)(DW_OP_reg0 + v->mach_reg));
          else { dwbuf_u8(&loc, DW_OP_regx); dwbuf_uleb(&loc, v->mach_reg); }
        } else if (v->loc_kind == MIR_DBLOC_FRAME) {
          dwbuf_u8(&loc, DW_OP_fbreg);
          dwbuf_sleb(&loc, v->loc.frame_offset);
        }
        dwbuf_uleb(dw_info, loc.len);
        if (loc.len > 0) dwbuf_bytes(dw_info, loc.data, loc.len);
        dwbuf_free(&loc);
      }
    }
    dwbuf_u8(dw_info, 0);
  }
  dwbuf_u8(dw_info, 0);

  for (size_t i = 0; i < n_tfix; i++) {
    uint32_t off = (type_off != NULL && tfix[i].id < num_types)
                     ? (uint32_t)type_off[tfix[i].id] : (uint32_t)void_off;
    dwbuf_patch_u32(dw_info, tfix[i].pos, off);
  }
  free(tfix);
  free(type_off);
#undef TYPE_REF

  uint32_t cu_len = (uint32_t)(dw_info->len - cu_start - 4);
  dwbuf_patch_u32(dw_info, cu_start, cu_len);

  /* .debug_line */
  size_t line_start = dw_line->len;
  dwbuf_u32(dw_line, 0);
  dwbuf_u16(dw_line, 4);
  size_t header_length_off = dw_line->len;
  dwbuf_u32(dw_line, 0);
  size_t after_header_len = dw_line->len;
  dwbuf_u8(dw_line, 1); dwbuf_u8(dw_line, 1); dwbuf_u8(dw_line, 1);
  dwbuf_u8(dw_line, (uint8_t)(int8_t)-5);
  dwbuf_u8(dw_line, 14); dwbuf_u8(dw_line, 13);
  dwbuf_u8(dw_line, 0); dwbuf_u8(dw_line, 1); dwbuf_u8(dw_line, 1); dwbuf_u8(dw_line, 1);
  dwbuf_u8(dw_line, 1); dwbuf_u8(dw_line, 0); dwbuf_u8(dw_line, 0); dwbuf_u8(dw_line, 0);
  dwbuf_u8(dw_line, 1); dwbuf_u8(dw_line, 0); dwbuf_u8(dw_line, 0); dwbuf_u8(dw_line, 1);
  dwbuf_u8(dw_line, 0);
  for (uint32_t fi = 1; fi <= mod->num_source_files; fi++) {
    const char *fn = mod->source_files[fi];
    if (fn == NULL) fn = "?";
    dwbuf_str(dw_line, fn);
    dwbuf_uleb(dw_line, 0); dwbuf_uleb(dw_line, 0); dwbuf_uleb(dw_line, 0);
  }
  dwbuf_u8(dw_line, 0);
  dwbuf_patch_u32(dw_line, header_length_off, (uint32_t)(dw_line->len - after_header_len));

  for (size_t fi = 0; fi < n_funcs; fi++) {
    const dwarf_aot_func_t *fe = &funcs[fi];
    MIR_func_t func = fe->func;
    if (func == NULL) continue;
    MIR_line_map_t *lm = (func->dbinfo != NULL) ? func->dbinfo->line_map : NULL;
    uint32_t first_line = 0, first_file = 0;
    if (lm != NULL && lm->num_entries > 0) {
      first_line = lm->entries[0].source_line;
      first_file = lm->entries[0].source_file_id;
    } else {
      for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, func->insns);
           insn != NULL; insn = DLIST_NEXT(MIR_insn_t, insn)) {
        if (insn->source_line != 0) {
          first_line = insn->source_line;
          first_file = insn->source_file_id;
          break;
        }
      }
    }
    if (first_line == 0) continue;
    dwbuf_u8(dw_line, 0); dwbuf_uleb(dw_line, 9); dwbuf_u8(dw_line, DW_LNE_set_address);
    DA_RELOC(dw_line->len, (int64_t)fe->text_offset, 1);
    dwbuf_u64(dw_line, 0);
    if (first_file != 0) { dwbuf_u8(dw_line, DW_LNS_set_file); dwbuf_uleb(dw_line, first_file); }
    if (lm != NULL && lm->num_entries > 0) {
      uint32_t prev_pc = 0, cur_line = 1;
      uint16_t cur_file = first_file;
      for (uint32_t li = 0; li < lm->num_entries; li++) {
        MIR_line_map_entry_t *e = &lm->entries[li];
        if (li > 0 && e->source_line == lm->entries[li-1].source_line
            && e->pc_offset == lm->entries[li-1].pc_offset) continue;
        if (e->source_line == cur_line && e->source_file_id == cur_file && li > 0) continue;
        if (e->pc_offset > prev_pc) {
          dwbuf_u8(dw_line, DW_LNS_advance_pc);
          dwbuf_uleb(dw_line, e->pc_offset - prev_pc);
          prev_pc = e->pc_offset;
        }
        if (e->source_file_id != cur_file) {
          dwbuf_u8(dw_line, DW_LNS_set_file);
          dwbuf_uleb(dw_line, e->source_file_id);
          cur_file = e->source_file_id;
        }
        if (e->source_col != 0) {
          dwbuf_u8(dw_line, DW_LNS_set_column);
          dwbuf_uleb(dw_line, e->source_col);
        }
        int32_t line_delta = (int32_t)e->source_line - (int32_t)cur_line;
        if (line_delta != 0) {
          dwbuf_u8(dw_line, DW_LNS_advance_line);
          dwbuf_sleb(dw_line, line_delta);
        }
        cur_line = e->source_line;
        dwbuf_u8(dw_line, DW_LNS_copy);
      }
      if (fe->code_len > prev_pc) {
        dwbuf_u8(dw_line, DW_LNS_advance_pc);
        dwbuf_uleb(dw_line, fe->code_len - prev_pc);
      }
    } else {
      dwbuf_u8(dw_line, DW_LNS_advance_line);
      dwbuf_sleb(dw_line, (int64_t)first_line - 1);
      dwbuf_u8(dw_line, DW_LNS_copy);
      dwbuf_u8(dw_line, DW_LNS_advance_pc);
      dwbuf_uleb(dw_line, fe->code_len);
    }
    dwbuf_u8(dw_line, 0); dwbuf_uleb(dw_line, 1); dwbuf_u8(dw_line, DW_LNE_end_sequence);
  }
  dwbuf_patch_u32(dw_line, line_start, (uint32_t)(dw_line->len - line_start - 4));
  } /* end full DIE/line */

#undef DA_RELOC
}

#else /* MIR_NO_DBINFO */

/* Stubs when debug info is compiled out. */
typedef struct { int unused; } dwarf_aot_result_t;
static void dwarf_aot_result_free(dwarf_aot_result_t *o) { (void)o; }

#endif /* !MIR_NO_DBINFO */
#endif /* DWARF_AOT_EMIT_H */
