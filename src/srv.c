#include "srv.h"

#include <string.h>

#include "svc.h"

#include "services.h"

u8 shared_font[] = {2, [0x80] =
#embed "../sys_files/font.bcfnt"
};

void srvobj_init(KObject* hdr, KObjType t) {
    hdr->type = t;
    hdr->refcount = 1;
}

u32 srvobj_make_handle(E3DS* s, KObject* o) {
    u32 handle = handle_new(s);
    if (!handle) return handle;
    HANDLE_SET(handle, o);
    o->refcount++;
    return handle;
}

void init_services(E3DS* s) {
    srvobj_init(&s->services.notif_sem.hdr, KOT_SEMAPHORE);

    srvobj_init(&s->services.apt.lock.hdr, KOT_MUTEX);
    srvobj_init(&s->services.apt.notif_event.hdr, KOT_EVENT);
    s->services.apt.notif_event.sticky = true;
    srvobj_init(&s->services.apt.resume_event.hdr, KOT_EVENT);
    s->services.apt.resume_event.sticky = true;
    s->services.apt.resume_event.signal = true;
    s->services.apt.nextparam.appid = APPID_HOMEMENU;
    s->services.apt.nextparam.cmd = APTCMD_WAKEUP;
    srvobj_init(&s->services.apt.shared_font.hdr, KOT_SHAREDMEM);
    s->services.apt.shared_font.defaultdata = shared_font;
    s->services.apt.shared_font.defaultdatalen = sizeof shared_font;
    s->services.apt.shared_font.vaddr = 0x20000000;
    s->services.apt.shared_font.size = sizeof shared_font;
    srvobj_init(&s->services.apt.capture_block.hdr, KOT_SHAREDMEM);
    s->services.apt.capture_block.size = 4 * (0x7000 + 2 * 0x19000);

    s->services.gsp.event = nullptr;
    srvobj_init(&s->services.gsp.sharedmem.hdr, KOT_SHAREDMEM);

    s->services.dsp.event = nullptr;
    srvobj_init(&s->services.dsp.semEvent.hdr, KOT_EVENT);

    srvobj_init(&s->services.hid.sharedmem.hdr, KOT_SHAREDMEM);
    for (int i = 0; i < HIDEVENT_MAX; i++) {
        srvobj_init(&s->services.hid.events[i].hdr, KOT_EVENT);
        s->services.hid.events[i].sticky = true;
    }
}

KSession* session_create(PortRequestHandler f) {
    KSession* session = calloc(1, sizeof *session);
    session->hdr.type = KOT_SESSION;
    session->handler = (PortRequestHandlerArg) f;
    return session;
}

KSession* session_create_arg(PortRequestHandlerArg f, u64 arg) {
    KSession* session = calloc(1, sizeof *session);
    session->hdr.type = KOT_SESSION;
    session->handler = f;
    session->arg = arg;
    return session;
}

DECL_PORT_ARG(stub, name) {
    u32* cmdbuf = PTR(cmd_addr);
    lwarn("stubbed service '%.8s' command 0x%04x (%x,%x,%x,%x,%x)",
          (char*) &name, cmd.command, cmdbuf[1], cmdbuf[2], cmdbuf[3],
          cmdbuf[4], cmdbuf[5]);
    cmdbuf[0] = IPCHDR(1, 0);
    cmdbuf[1] = 0;
}

DECL_PORT(srv) {
#define IS(_name) (!strncmp(name, _name, 8))
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001: {
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0002: {
            cmdbuf[0] = IPCHDR(1, 2);
            u32 h = srvobj_make_handle(s, &s->services.notif_sem.hdr);
            if (h) {
                cmdbuf[3] = h;
                cmdbuf[1] = 0;
            } else {
                cmdbuf[1] = -1;
            }
            break;
        }
        case 0x0005: {
            char* name = (char*) &cmdbuf[1];
            name[cmdbuf[3]] = '\0';

            PortRequestHandler handler;
            if (IS("APT:U") || IS("APT:A") || IS("APT:S")) {
                handler = port_handle_apt;
            } else if (IS("fs:USER")) {
                handler = port_handle_fs;
            } else if (IS("gsp::Gpu")) {
                handler = port_handle_gsp_gpu;
            } else if (IS("hid:USER") || IS("hid:SPVR")) {
                handler = port_handle_hid;
            } else if (IS("dsp::DSP")) {
                handler = port_handle_dsp;
            } else if (IS("cfg:u")) {
                handler = port_handle_cfg;
            } else if (IS("y2r:u")) {
                handler = port_handle_y2r;
            } else {
                lerror("unknown service '%.8s'", name);
                u32 handle = handle_new(s);
                KSession* session =
                    session_create_arg(port_handle_stub, *(u64*) name);
                HANDLE_SET(handle, session);
                session->hdr.refcount = 1;
                cmdbuf[3] = handle;
                linfo("connected to unknown service '%.8s' with handle %x",
                      name, handle);
                cmdbuf[0] = IPCHDR(3, 0);
                cmdbuf[1] = 0;
                break;
            }
            u32 handle = handle_new(s);
            KSession* session = session_create(handler);
            HANDLE_SET(handle, session);
            session->hdr.refcount = 1;
            cmdbuf[3] = handle;
            linfo("connected to service '%s' with handle %x", name, handle);
            cmdbuf[0] = IPCHDR(3, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0009:
            linfo("Subscribe to notification %x", cmdbuf[1]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x000b:
            linfo("RecieveNotiifcation");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
#undef IS
}

DECL_PORT(errf) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001: {
            struct {
                u32 ipcheader;
                u8 type;
                u8 revh;
                u16 revl;
                u32 resultcode;
                u32 pc;
                u32 pid;
                u8 title[8];
                u8 applet[8];
                union {
                    char message[0x60];
                };
            }* errinfo = (void*) cmdbuf;

            lerror("fatal error type %d, result %08x, pc %08x, message: %s",
                   errinfo->type, errinfo->resultcode, errinfo->pc,
                   errinfo->message);
            exit(1);
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