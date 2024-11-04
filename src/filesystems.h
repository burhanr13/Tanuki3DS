#ifndef FILESYSTEMS_H
#define FILESYSTEMS_H

#include "common.h"

typedef struct {
    u8 signature[0x100];
    char magic[4];
    u32 size;
    u64 media_id;
    u8 part_type[8];
    u8 part_crypt[8];
    struct {
        u32 offset;
        u32 size;
    } part[8];
} NCSDHeader;

typedef struct {
    u8 signature[0x100];
    char magic[4];
    u32 size;
    u64 part_id;
    u8 _110[0x90];
    struct {
        u32 offset;
        u32 size;
        u32 hdrsize;
        u32 res;
    } exefs, romfs;
    u8 exefs_hash[0x20];
    u8 romfs_hash[0x20];
} NCCHHeader;

typedef struct {
    struct {
        char apptitle[8];
        u8 res_008[5];
        union {
            u8 b;
            struct {
                u8 compressed : 1;
                u8 sdapp : 1;
            };
        } flags;
        u16 remasterversion;
        union {
            struct {
                struct {
                    u32 vaddr;
                    u32 pages;
                    u32 size;
                    u32 _pad;
                } text, rodata, data;
            };
            struct {
                u32 pad1[3];
                u32 stacksz;
                u32 pad2[7];
                u32 bss;
            };
        };
    } sci;
} ExHeader;

typedef struct {
    struct {
        char name[8];
        u32 offset;
        u32 size;
    } file[10];
} ExeFSHeader;

typedef struct __attribute__((packed)) {
    u8 magic[4];
    u32 magic2;
    u32 masterhashsize;
    struct {
        u64 offset;
        u64 size;
        u32 blksize;
        u32 res;
    } lvl[3];
    u32 _54;
    u32 infosize;
} IVFCHeader;

#endif