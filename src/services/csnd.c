#include "csnd.h"

#include "3ds.h"

DECL_PORT(csnd) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001: {
            u32 shmem = srvobj_make_handle(s, &s->services.csnd.shmem.hdr);
            linfo("Initialize shmem handle %08x", shmem);
            cmdbuf[0] = IPCHDR(1, 3);
            cmdbuf[1] = 0;
            cmdbuf[4] = shmem;
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
