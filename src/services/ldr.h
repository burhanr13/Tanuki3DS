#ifndef LDR_H
#define LDR_H

#include "../srv.h"

typedef union {
    u32 raw;
    struct {
        u32 segment : 4;
        u32 offset : 28;
    };
} SegmentOffset;

typedef struct {
    u32 addr;
    u32 size;
} CROField;

typedef struct {
    u8 hash[0x80];
    char magic[4]; // CRO0
    u32 name_addr;
    u32 next;
    u32 prev;
    u32 size;
    u32 bsssz;
    u32 _unk98;
    u32 _unk9c;
    SegmentOffset nnrocontrolobject;
    SegmentOffset onload;
    SegmentOffset onexit;
    SegmentOffset onunresolved;
    CROField code;
    CROField data;
    CROField modulename;
    CROField segmenttable;
    struct {
        CROField named_symbols;
        CROField indexed_symbols;
        CROField strings;
        CROField nametree;
    } exports;
    CROField import_table;
    CROField external_patches;
    struct {
        CROField named_symbols;
        CROField indexed_symbols;
        CROField anon_symbols;
        CROField strings;
    } imports;
    CROField static_anon_symbols;
    CROField internal_patches;
    CROField static_anon_patches;
} CROHeader;

DECL_PORT(ldr_ro);

void cro_relocate_hdr(E3DS* s, u32 vaddr);

#endif
