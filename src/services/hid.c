#include "hid.h"

#include "../3ds.h"

DECL_PORT(hid) {
    u32* cmd_params = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x000a:
            cmd_params[0] = MAKE_IPCHEADER(1, 7);
            cmd_params[1] = 0;
            cmd_params[2] = 0x14000000;
            cmd_params[3] =
                srvobj_make_handle(s, &s->services.hid.sharedmem.hdr);
            for (int i = 0; i < HIDEVENT_MAX; i++) {
                cmd_params[4 + i] =
                    srvobj_make_handle(s, &s->services.hid.events[i].hdr);
            }
            linfo("GetIPCHandles with sharedmem %x", cmd_params[3]);
            break;
        case 0x0011:
            linfo("EnableAccelerometer");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x0013:
            linfo("EnableGyroscopeLow");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x0015:
            linfo("GetGyroscopeLowRawToDpsCoefficient");
            cmd_params[0] = MAKE_IPCHEADER(2, 0);
            cmd_params[1] = 0;
            *(float*) &cmd_params[2] = 1.0f;
            break;
        case 0x0016:
            linfo("GetGyroscopeLowCalibrateParam");
            cmd_params[0] = MAKE_IPCHEADER(6, 0);
            cmd_params[1] = 0;
            cmd_params[2] = -1;
            cmd_params[3] = -1;
            cmd_params[4] = -1;
            cmd_params[5] = -1;
            cmd_params[6] = -1;
            break;
        default:
            lwarn("unknown command 0x%04x", cmd.command);
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = -1;
            break;
    }
}
