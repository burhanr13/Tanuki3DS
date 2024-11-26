#include "cfg.h"

#include <wchar.h>

#include "../3ds.h"

DECL_PORT(cfg) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001: {
            int blkid = cmdbuf[2];
            void* ptr = PTR(cmdbuf[4]);
            linfo("GetConfigInfoBlk2 with blkid %x", blkid);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;

            // these are currently set to reasonable defaults
            // in the future it should be possible to change these
            switch (blkid) {
                case 0x50005: {
                    float* block = ptr;
                    block[0] = 62.0f;
                    block[1] = 289.0f;
                    block[2] = 76.80f;
                    block[3] = 46.08f;
                    block[4] = 10.0f;
                    block[5] = 5.0f;
                    block[6] = 55.58f;
                    block[7] = 21.57f;
                    break;
                }
                case 0x70001:
                    *(u8*) ptr = 0;
                    break;
                case 0xa0000: {
                    u8 name[] = "ctremu";
                    u16* dst = ptr;
                    for (int i = 0; i < sizeof name; i++) {
                        dst[i] = name[i];
                    }
                    break;
                }
                case 0xa0002:
                    *(u8*) ptr = 1;
                    break;
                case 0xb0000:
                    ((u8*) ptr)[2] = 1;
                    ((u8*) ptr)[3] = 49;
                    break;
                case 0xb0001: {
                    u16(*tbl)[0x40] = ptr;
                    u8 name[] = "some country";
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < sizeof name; j++) {
                            tbl[i][j] = name[j];
                        }
                    }
                    break;
                }
                case 0xb0002: {
                    u16(*tbl)[0x40] = ptr;
                    u8 name[] = "some state";
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < sizeof name; j++) {
                            tbl[i][j] = name[j];
                        }
                    }
                    break;
                }
                case 0xb0003:
                    ((s16*) ptr)[0] = 0;
                    ((s16*) ptr)[1] = 0;
                    break;
                case 0xc0000:
                    ((u32*) ptr)[0] = 0;
                    ((u32*) ptr)[1] = 0;
                    ((u32*) ptr)[2] = 0;
                    ((u32*) ptr)[3] = 0;
                    ((u32*) ptr)[4] = 0;
                    break;
                case 0xd0000:
                    ((u16*) ptr)[0] = 1;
                    ((u16*) ptr)[1] = 1;
                    break;
                case 0x130000:
                    *(u32*) ptr = 0;
                    break;
                default:
                    lwarn("unknown blkid %x", blkid);
                    break;
            }
            break;
        }
        case 0x0002:
            linfo("GetRegion");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 1;
            break;
        case 0x0003:
            linfo("GenHashConsoleUnique");
            cmdbuf[0] = IPCHDR(3, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0x69696969;
            cmdbuf[3] = 0x69696969;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}
