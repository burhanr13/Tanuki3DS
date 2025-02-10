#include "cecd.h"

#include <3ds.h>

DECL_PORT(cecd) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x000f: {
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.cecd.cecinfo.hdr);
            linfo("GetCecInfoEventHandle with handle %x", cmdbuf[3]);
            break;
        }
        case 0x0012: {
            linfo("OpenAndRead");
            cmdbuf[0] = IPCHDR(2, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[4] = cmdbuf[8];
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