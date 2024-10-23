#include "frd.h"

#include "../3ds.h"

DECL_PORT(frd) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}