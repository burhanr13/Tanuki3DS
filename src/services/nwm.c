#include "nwm.h"

#include "3ds.h"

DECL_PORT(nwm_uds) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x001b:
            linfo("InitializeWithVersion");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = -1;
            break;
    }
}