#ifndef ETC1_H
#define ETC1_H

#include "common.h"

typedef union {
    u64 d;
    struct {
        u16 modidx;
        u16 modneg;
        struct {
            u8 flip : 1;
            u8 diff : 1;
            u8 table2 : 3;
            u8 table1 : 3;
        };
        union {
            struct {
                u8 b2 : 4;
                u8 b1 : 4;
                u8 g2 : 4;
                u8 g1 : 4;
                u8 r2 : 4;
                u8 r1 : 4;
            };
            struct {
                s8 db2 : 3;
                u8 db1 : 5;
                s8 dg2 : 3;
                u8 dg1 : 5;
                s8 dr2 : 3;
                u8 dr1 : 5;
            };
        };
    };
} etc1block;

void etc1_decompress_texture(u32 width, u32 height, u64 (*src)[width / 8][2][2],
                             u8 (*dst)[width][3]);
void etc1a4_decompress_texture(u32 width, u32 height,
                               u64 (*src)[width / 8][2][2][2],
                               u8 (*dst)[width][4]);

#endif
