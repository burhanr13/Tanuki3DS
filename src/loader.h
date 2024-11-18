#ifndef LOADER_H
#define LOADER_H

#include "common.h"

typedef struct _3DS HLE3DS;

typedef struct {
    FILE* fp;
    u32 exheader_off;
    u32 exefs_off;
    u32 romfs_off;
} RomImage;

u32 load_elf(HLE3DS* s, char* filename);
u32 load_ncsd(HLE3DS* s, char* filename);
u32 load_ncch(HLE3DS* s, char* filename, u64 offset);

u8* lzssrev_decompress(u8* in, u32 src_size, u32* dst_size);

#endif
