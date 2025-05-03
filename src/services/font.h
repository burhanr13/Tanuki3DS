#ifndef FONT_H
#define FONT_H

#include "3ds.h"
#include "common.h"

// font structs from libctru

typedef struct {
    s8 left;
    u8 glyphWidth;
    u8 charWidth;
} charWidthInfo_s;

typedef struct {
    u16 startIndex;
    u16 endIndex;
    u32 pNext;
    charWidthInfo_s widths[0];
} CWDH_s;

typedef struct {
    u8 cellWidth;
    u8 cellHeight;
    u8 baselinePos;
    u8 maxCharWidth;
    u32 sheetSize;
    u16 nSheets;
    u16 sheetFmt;
    u16 nRows;
    u16 nLines;
    u16 sheetWidth;
    u16 sheetHeight;
    u32 pSheetData;
} TGLP_s;

typedef struct {
    u16 codeBegin;
    u16 codeEnd;
    u16 mappingMethod;
    u16 reserved;
    u32 pNext;
    union {
        u16 indexOffset;
        u16 indexTable[0];
        struct {
            u16 nScanEntries;
            struct {
                u16 code;
                u16 glyphIndex;
            } scanEntries[0];
        };
    };
} CMAP_s;

typedef struct {
    u32 signature;
    u32 sectionSize;
    u8 fontType;
    u8 lineFeed;
    u16 alterCharIndex;
    charWidthInfo_s defaultWidth;
    u8 encoding;
    u32 pTglp;
    u32 pCwdh;
    u32 pCmap;
    u8 height;
    u8 width;
    u8 ascent;
    u8 padding;
} FINF_s;

typedef struct {
    u8 signature[4]; // CFNT before relocation, CFNU after
    u16 endianness;
    u16 headerSize;
    u32 version;
    u32 fileSize;
    u32 nBlocks;

    FINF_s finf;
} CFNT_s;

void font_load(E3DS* s);
void font_relocate(void* buf, u32 vaddr);

#endif
