#include "apt.h"

#include "../3ds.h"

DECL_PORT(apt) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001:
            cmdbuf[0] = IPCHDR(3, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = 0;
            cmdbuf[4] = 0;
            cmdbuf[5] = srvobj_make_handle(s, &s->services.apt.lock.hdr);
            linfo("GetLockHandle is %x", cmdbuf[5]);
            break;
        case 0x0002:
            cmdbuf[0] = IPCHDR(1, 3);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0x04000000;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.apt.notif_event.hdr);
            cmdbuf[4] =
                srvobj_make_handle(s, &s->services.apt.resume_event.hdr);
            linfo("Initialize with notif event %x and resume event %x",
                  cmdbuf[3], cmdbuf[4]);
            break;
        case 0x0003:
            linfo("Enable");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0006: {
            u32 appid = cmdbuf[1];
            linfo("GetAppletInfo for 0x%x", appid);
            cmdbuf[0] = IPCHDR(7, 0);
            cmdbuf[1] = 0;
            cmdbuf[5] = 1;
            cmdbuf[6] = 1;
            break;
        }
        case 0x0009: {
            u32 appid = cmdbuf[1];
            linfo("IsRegistered for 0x%x", appid);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 1;
            break;
        }
        case 0x000d:
            linfo("ReceiveParameter");
            cmdbuf[0] = IPCHDR(4, 4);
            cmdbuf[1] = 0;
            cmdbuf[3] = 1;
            break;
        case 0x000e:
            linfo("GlanceParameter");
            cmdbuf[0] = IPCHDR(4, 4);
            cmdbuf[1] = 0;
            cmdbuf[3] = 1;
            break;
        case 0x0043:
            linfo("NotifyToWait");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x004b:
            linfo("AppletUtility");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            break;
        case 0x004f: {
            int percent = cmdbuf[2];
            linfo("SetApplicationCpuTimeLimit to %d%%", percent);
            s->services.apt.application_cpu_time_limit = percent;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0050: {
            linfo("GetApplicationCpuTimeLimit");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = s->services.apt.application_cpu_time_limit;
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
