#ifndef LDR_H
#define LDR_H

#include "../srv.h"

typedef struct {
    u32 crs_addr;
} LDRData;

enum {
    CROSEG_TEXT,
    CROSEG_RODATA,
    CROSEG_DATA,
    CROSEG_BSS,
};

typedef union {
    u32 raw;
    struct {
        u32 id : 4;
        u32 offset : 28;
    };
} SegmentTag;

typedef struct {
    u32 addr;
    u32 size;
} CROField;

typedef struct {
    u32 addr;
    u32 size;
    u32 id;
} CROSegment;

typedef struct {
    u32 name_addr;
    SegmentTag loc;
} CRONamedExport;

typedef struct {
    SegmentTag loc;
} CROIndexedExport;

typedef struct {
    u32 name_addr;
    u32 patches_addr;
} CRONamedImport;

typedef struct {
    u32 index;
    u32 patches_addr;
} CROIndexedImport;

typedef struct {
    SegmentTag loc;
    u32 patches_addr;
} CROAnonImport;

typedef struct {
    u32 name_addr;
    CROField indexed;
    CROField anon;
} CROImportModule;

typedef struct {
    SegmentTag loc;
    u8 type;
    u8 end;
    u8 loaded;
    u8 _pad;
    u32 addend;
} CROImportPatch;

typedef struct {
    SegmentTag loc;
    u8 type;
    u8 segid;
    u8 _pad[2];
    u32 addend;
} CROInternalPatch;

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
    SegmentTag nnrocontrolobject;
    SegmentTag onload;
    SegmentTag onexit;
    SegmentTag onunresolved;
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
    CROField import_modules;
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

void ldr_load_cro(E3DS* s, u32 vaddr, u32 data, u32 bss, bool autolink);

#endif
