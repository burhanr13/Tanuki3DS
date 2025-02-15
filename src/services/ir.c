#include "ir.h"

#include "3ds.h"

DECL_PORT(ir) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x000c: {
            u32 h = srvobj_make_handle(s, &s->services.ir.event.hdr);
            linfo("GetConnectionStatusEvent with handle %08x", h);
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = h;
            break;
        }
        case 0x0018:
            // theres some shared memory stuff too but thats for later
            linfo("InitializeIrnopShared");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}