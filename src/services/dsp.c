#include "dsp.h"

#include "../3ds.h"

DECL_PORT(dsp) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0c: {
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[2] = DSPMEM + (cmdbuf[1] << 1);
            cmdbuf[1] = 0;
            break;
        }
        case 0x10: {
            u32 size = (u16) cmdbuf[3];
            void* buf = PTR(cmdbuf[0x41]);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = size;
            if (size == 2) {
                *(u16*) buf = 15;
            }
            linfo("ReadPipeIfPossible with size 0x%x", size);
            break;
        }
        case 0x0011:
            linfo("LoadComponent");
            cmdbuf[0] = IPCHDR(2, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = true;
            cmdbuf[3] = 0;
            cmdbuf[4] = 0;
            break;
        case 0x0013:
            linfo("FlushDataCache");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0015:
            linfo("RegisterInterruptEvents with handle %x", cmdbuf[4]);
            s->services.dsp.event = HANDLE_GET_TYPED(cmdbuf[4], KOT_EVENT);
            if (s->services.dsp.event) {
                s->services.dsp.event->hdr.refcount++;
                s->services.dsp.event->sticky = true;
                s->services.dsp.event->signal = true;
            }
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0016:
            linfo("GetSemaphoreEventHandle");
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.dsp.semEvent.hdr);
            break;
        case 0x001f:
            linfo("GetHeadphoneStatus");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = false;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}
