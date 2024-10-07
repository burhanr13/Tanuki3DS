#include "apt.h"

#include "../3ds.h"

DECL_PORT(apt) {
    u32* cmd_params = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001:
            cmd_params[0] = MAKE_IPCHEADER(3, 2);
            cmd_params[1] = 0;
            cmd_params[2] = 0;
            cmd_params[3] = 0;
            cmd_params[4] = 0;
            cmd_params[5] = srvobj_make_handle(s, &s->services.apt.lock.hdr);
            linfo("GetLockHandle is %x", cmd_params[5]);
            break;
        case 0x0002:
            cmd_params[0] = MAKE_IPCHEADER(1, 3);
            cmd_params[1] = 0;
            cmd_params[2] = 0x04000000;
            cmd_params[3] =
                srvobj_make_handle(s, &s->services.apt.notif_event.hdr);
            cmd_params[4] =
                srvobj_make_handle(s, &s->services.apt.resume_event.hdr);
            linfo("Initialize with notif event %x and resume event %x",
                  cmd_params[3], cmd_params[4]);
            break;
        case 0x0003:
            linfo("Enable");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x0006: {
            u32 appid = cmd_params[1];
            linfo("GetAppletInfo for 0x%x", appid);
            cmd_params[0] = MAKE_IPCHEADER(7, 0);
            cmd_params[1] = 0;
            cmd_params[5] = 1;
            cmd_params[6] = 1;
            break;
        }
        case 0x000d:
            linfo("ReceiveParameter");
            cmd_params[0] = MAKE_IPCHEADER(4, 4);
            cmd_params[1] = 0;
            cmd_params[3] = 1;
            break;
        case 0x000e:
            linfo("GlanceParameter");
            cmd_params[0] = MAKE_IPCHEADER(4, 4);
            cmd_params[1] = 0;
            cmd_params[3] = 1;
            break;
        case 0x0043:
            linfo("NotifyToWait");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x004b:
            linfo("AppletUtility");
            cmd_params[0] = MAKE_IPCHEADER(2, 0);
            cmd_params[1] = 0;
            cmd_params[2] = 0;
            break;
        case 0x004f: {
            int percent = cmd_params[2];
            linfo("SetApplicationCpuTimeLimit to %d%%", percent);
            s->services.apt.application_cpu_time_limit = percent;
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        }
        case 0x0050: {
            linfo("GetApplicationCpuTimeLimit");
            cmd_params[0] = MAKE_IPCHEADER(2, 0);
            cmd_params[1] = 0;
            cmd_params[2] = s->services.apt.application_cpu_time_limit;
            break;
        }
        default:
            lwarn("unknown command 0x%04x", cmd.command);
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
    }
}
