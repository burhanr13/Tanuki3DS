#include "y2r.h"

#include "3ds.h"

void y2r_event(E3DS* s) {
    s->services.y2r.busy = false;
    if (s->services.y2r.enableInterrupt) {
        event_signal(s, &s->services.y2r.transferend);
    }
}

DECL_PORT(y2r) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x000d: {
            s->services.y2r.enableInterrupt = cmdbuf[1];
            linfo("SetTransferEndInterrupt");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x000f: {
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.y2r.transferend.hdr);
            linfo("GetTransferEndEvent with handle %x", cmdbuf[3]);
            break;
        }
        case 0x0026:
            linfo("StartConversion");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            // todo: actually do the conversion

            s->services.y2r.busy = true;
            // things break if this is instant
            add_event(&s->sched, (SchedulerCallback) y2r_event, 0, 10000);
            break;
        case 0x0027:
            linfo("StopConversion");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            remove_event(&s->sched, (SchedulerCallback) y2r_event, 0);
            s->services.y2r.busy = false;
            break;
        case 0x0028:
            linfo("IsBusyConversion");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = s->services.y2r.busy;
            break;
        case 0x002a:
            linfo("PingProcess");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 1; // connected
            break;
        case 0x002b:
            linfo("DriverInitialize");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        default:
            linfo("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}