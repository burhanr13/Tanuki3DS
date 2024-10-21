#include "etc1.h"

const u8 etc1table[8][2] = {
    {2, 8},   {5, 17},  {9, 29},   {13, 42},
    {18, 60}, {24, 80}, {33, 106}, {47, 183},
};

#define CLAMP(x) ((x) < 0 ? 0 : (x) > 255 ? 255 : (x))

void etc1_decompress_block(u64 block, u32 width, int Bpp,
                           u8 (*dst)[width][Bpp]) {
    etc1block blk = {block};

    u8 r1, r2, g1, g2, b1, b2;
    if (blk.diff) {
        r1 = blk.dr1 * 0x21 / 4;
        r2 = (blk.dr1 + blk.dr2) * 0x21 / 4;
        g1 = blk.dg1 * 0x21 / 4;
        g2 = (blk.dg1 + blk.dg2) * 0x21 / 4;
        b1 = blk.db1 * 0x21 / 4;
        b2 = (blk.db1 + blk.db2) * 0x21 / 4;
    } else {
        r1 = blk.r1 * 0x11;
        r2 = blk.r2 * 0x11;
        g1 = blk.g1 * 0x11;
        g2 = blk.g2 * 0x11;
        b1 = blk.b1 * 0x11;
        b2 = blk.b2 * 0x11;
    }

    for (int i = 0; i < 8; i++) {
        int x, y;
        if (blk.flip) {
            x = i % 4;
            y = i / 4;
        } else {
            x = i / 4;
            y = i % 4;
        }
        int pixel = y + x * 4;
        int mod = etc1table[blk.table1][(blk.modidx >> pixel) & 1];
        if ((blk.modneg >> pixel) & 1) {
            dst[y][x][0] = CLAMP(r1 - mod);
            dst[y][x][1] = CLAMP(g1 - mod);
            dst[y][x][2] = CLAMP(b1 - mod);
        } else {
            dst[y][x][0] = CLAMP(r1 + mod);
            dst[y][x][1] = CLAMP(g1 + mod);
            dst[y][x][2] = CLAMP(b1 + mod);
        }
    }
    for (int i = 0; i < 8; i++) {
        int x, y;
        if (blk.flip) {
            x = i % 4;
            y = i / 4 + 2;
        } else {
            x = i / 4 + 2;
            y = i % 4;
        }
        int pixel = y + x * 4;
        int mod = etc1table[blk.table2][(blk.modidx >> pixel) & 1];
        if ((blk.modneg >> pixel) & 1) {
            dst[y][x][0] = CLAMP(r2 - mod);
            dst[y][x][1] = CLAMP(g2 - mod);
            dst[y][x][2] = CLAMP(b2 - mod);
        } else {
            dst[y][x][0] = CLAMP(r2 + mod);
            dst[y][x][1] = CLAMP(g2 + mod);
            dst[y][x][2] = CLAMP(b2 + mod);
        }
    }
}

u8* etc1_decompress_texture(u32 width, u32 height,
                            u64 (*src)[width / 8][2][2]) {
    u8(*dst)[width][3] = malloc(height * sizeof *dst);

    for (int tx = 0; tx < width / 8; tx++) {
        for (int ty = 0; ty < height / 8; ty++) {
            for (int ttx = 0; ttx < 2; ttx++) {
                for (int tty = 0; tty < 2; tty++) {
                    etc1_decompress_block(
                        src[ty][tx][tty][ttx], width, 3,
                        (void*) &dst[8 * ty + 4 * tty][8 * tx + 4 * ttx]);
                }
            }
        }
    }

    return (u8*) dst;
}

u8* etc1a4_decompress_texture(u32 width, u32 height,
                              u64 (*src)[width / 8][2][2][2]) {
    u8(*dst)[width][4] = malloc(height * sizeof *dst);

    for (int tx = 0; tx < width / 8; tx++) {
        for (int ty = 0; ty < height / 8; ty++) {
            for (int ttx = 0; ttx < 2; ttx++) {
                for (int tty = 0; tty < 2; tty++) {
                    u64 etcblk = src[ty][tx][tty][ttx][1];
                    u64 ablk = src[ty][tx][tty][ttx][0];
                    etc1_decompress_block(
                        etcblk, width, 4,
                        (void*) &dst[8 * ty + 4 * tty][8 * tx + 4 * ttx]);
                    for (int fx = 0; fx < 4; fx++) {
                        for (int fy = 0; fy < 4; fy++) {
                            int pixel = fy + fx * 4;
                            u8 a = ((ablk >> (pixel * 4)) & 0xf) * 0x11;
                            dst[8 * ty + 4 * tty + fy][8 * tx + 4 * ttx + fx]
                               [3] = a;
                        }
                    }
                }
            }
        }
    }

    return (u8*) dst;
}