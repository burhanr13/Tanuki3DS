#include "dsp.h"

#include "../3ds.h"

DECL_PORT(dsp) {
    u32* cmd_params = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0c: {
            cmd_params[0] = MAKE_IPCHEADER(2, 0);
            cmd_params[2] = DSPMEM + (cmd_params[1] << 1);
            cmd_params[1] = 0;
            break;
        }
        case 0x10: {
            u32 size = (u16) cmd_params[3];
            void* buf = PTR(cmd_params[0x41]);
            cmd_params[0] = MAKE_IPCHEADER(2, 0);
            cmd_params[1] = 0;
            cmd_params[2] = size;
            if (size == 2) {
                *(u16*) buf = 15;
            }
            linfo("ReadPipeIfPossible with size 0x%x", size);
            break;
        }
        case 0x0011:
            linfo("LoadComponent");
            cmd_params[0] = MAKE_IPCHEADER(2, 2);
            cmd_params[1] = 0;
            cmd_params[2] = true;
            cmd_params[3] = 0;
            cmd_params[4] = 0;
            break;
        case 0x0013:
            linfo("FlushDataCache");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x0015:
            linfo("RegisterInterruptEvents with handle %x", cmd_params[4]);
            s->services.dsp.event = HANDLE_GET_TYPED(cmd_params[4], KOT_EVENT);
            if (s->services.dsp.event) {
                s->services.dsp.event->hdr.refcount++;
                s->services.dsp.event->sticky = true;
                s->services.dsp.event->signal = true;
            }
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x0016:
            linfo("GetSemaphoreEventHandle");
            cmd_params[0] = MAKE_IPCHEADER(1, 2);
            cmd_params[1] = 0;
            cmd_params[2] = 0;
            cmd_params[3] =
                srvobj_make_handle(s, &s->services.dsp.semEvent.hdr);
            break;
        case 0x001f:
            linfo("GetHeadphoneStatus");
            cmd_params[0] = MAKE_IPCHEADER(2, 0);
            cmd_params[1] = 0;
            cmd_params[2] = false;
            break;
        default:
            lwarn("unknown command 0x%04x", cmd.command);
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
    }
}
