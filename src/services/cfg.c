#include "cfg.h"

#include "3ds.h"
#include "emulator.h"
#include "unicode.h"

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
                case 0x30001: { // user time offset
                    *(u64*) ptr = 0;
                    break;
                }
                case 0x50005: { // stereo display settings
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
                case 0x70001: // sound output mode
                    *(u8*) ptr = ctremu.audiomode;
                    break;
                case 0xa0000: { // user name
                    convert_to_utf16(ptr, 0x1c, ctremu.username);
                    break;
                }
                case 0xa0001: {         // birthday
                    ((u8*) ptr)[0] = 1; // month
                    ((u8*) ptr)[1] = 1; // day
                    break;
                }
                case 0xa0002: // language
                    *(u8*) ptr = ctremu.language;
                    break;
                case 0xb0000:            // country info
                    ((u8*) ptr)[2] = 1;  // state
                    ((u8*) ptr)[3] = 49; // country
                    break;
                case 0xb0001: {
                    u16(*tbl)[0x40] = ptr;
                    for (int i = 0; i < 16; i++) {
                        convert_to_utf16(tbl[i], 0x40, "some country");
                    }
                    break;
                }
                case 0xb0002: { // state name
                    u16(*tbl)[0x40] = ptr;
                    for (int i = 0; i < 16; i++) {
                        convert_to_utf16(tbl[i], 0x40, "some state");
                    }
                    break;
                }
                case 0xb0003: // coordinates
                    ((s16*) ptr)[0] = 0;
                    ((s16*) ptr)[1] = 0;
                    break;
                case 0xc0000: // parental controls
                    ((u32*) ptr)[0] = 0;
                    ((u32*) ptr)[1] = 0;
                    ((u32*) ptr)[2] = 0;
                    ((u32*) ptr)[3] = 0;
                    ((u32*) ptr)[4] = 0;
                    break;
                case 0xd0000: // eula version
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
            cmdbuf[2] =
                ctremu.detectRegion ? s->romimage.region : ctremu.region;
            break;
        case 0x0003:
            linfo("GenHashConsoleUnique");
            cmdbuf[0] = IPCHDR(3, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0x69696969;
            cmdbuf[3] = 0x69696969;
            break;
        case 0x0004:
            linfo("GetRegionCanadaUSA");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            break;
        case 0x0005:
            linfo("GetSystemModel");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0; // old 3ds
            break;
        case 0x0009:
            linfo("GetCountryCodeString");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 'X' | 'X' << 8; // dummy code
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}
