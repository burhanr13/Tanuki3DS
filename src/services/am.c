#include "am.h"

#include "3ds.h"

DECL_PORT(am) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x1001: {
            lwarn("GetDLCContentInfoCount");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 10; // surely no game has more than 10 dlcs
            break;
        }
        case 0x1003: {
            lwarn("ListDLCContentInfos");
            int count = cmdbuf[1];
            void* buf = PTR(cmdbuf[7]);

            // report each dlc is downloaded and owned
            for (int i = 0; i < count; i++) {
                *(u8*) (buf + 24 * i + 16) = 3;
            }

            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = count;
            break;
        }
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}