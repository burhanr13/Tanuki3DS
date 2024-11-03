#include "mic.h"

#include "../3ds.h"

DECL_PORT(mic) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0008: {
            linfo("SetGain");
            s->services.mic.gain = cmdbuf[1];
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0009: {
            linfo("GetGain");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = s->services.mic.gain;
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