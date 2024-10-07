#include "srv.h"

#include <string.h>

#include "svc.h"

#include "service_data.h"

void srvobj_init(KObject* hdr, KObjType t) {
    hdr->type = t;
    hdr->refcount = 1;
}

u32 srvobj_make_handle(HLE3DS* s, KObject* o) {
    u32 handle = handle_new(s);
    if (!handle) return handle;
    HANDLE_SET(handle, o);
    o->refcount++;
    return handle;
}

void init_services(HLE3DS* s) {
    srvobj_init(&s->services.notif_sem.hdr, KOT_SEMAPHORE);

    srvobj_init(&s->services.apt.lock.hdr, KOT_MUTEX);
    srvobj_init(&s->services.apt.notif_event.hdr, KOT_EVENT);
    s->services.apt.notif_event.sticky = true;
    srvobj_init(&s->services.apt.resume_event.hdr, KOT_EVENT);
    s->services.apt.resume_event.sticky = true;
    s->services.apt.resume_event.signal = true;

    s->services.gsp.event = NULL;
    srvobj_init(&s->services.gsp.sharedmem.hdr, KOT_SHAREDMEM);

    s->services.dsp.event = NULL;
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
    session->handler = f;
    return session;
}

DECL_PORT(srv) {
#define IS(_name) (!strcmp(name, _name))
    u32* cmd_params = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001: {
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        }
        case 0x0002: {
            cmd_params[0] = MAKE_IPCHEADER(1, 2);
            u32 h = srvobj_make_handle(s, &s->services.notif_sem.hdr);
            if (h) {
                cmd_params[3] = h;
                cmd_params[1] = 0;
            } else {
                cmd_params[1] = -1;
            }
            break;
        }
        case 0x0005: {
            char name[9];
            memcpy(name, &cmd_params[1], cmd_params[3]);
            name[cmd_params[3]] = '\0';
            cmd_params[0] = MAKE_IPCHEADER(3, 0);
            cmd_params[1] = 0;

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
            } else if (IS("ndm:u")) {
                handler = port_handle_ndm;
            } else if (IS("cecd:u")) {
                handler = port_handle_cecd;
            } else if (IS("mic:u")) {
                handler = port_handle_mic;
            } else if (IS("frd:u")) {
                handler = port_handle_frd;
            } else {
                lerror("unknown service '%s'", name);
                cmd_params[1] = -1;
            }
            if (cmd_params[1] == 0) {
                u32 handle = handle_new(s);
                KSession* session = session_create(handler);
                HANDLE_SET(handle, session);
                session->hdr.refcount = 1;
                cmd_params[3] = handle;
                linfo("connected to service '%s' with handle %x", name, handle);
            }
            break;
        }
        case 0x000b:
            cmd_params[0] = MAKE_IPCHEADER(2, 0);
            cmd_params[1] = 0;
            cmd_params[2] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x", cmd.command);
            cmd_params[1] = 0;
            cmd_params[2] = 0;
            break;
    }
#undef IS
}

DECL_PORT(errf) {
    u32* cmd_params = PTR(cmd_addr);
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
            }* errinfo = (void*) cmd_params;

            lerror("fatal error type %d, result %08x, pc %08x, message: %s",
                   errinfo->type, errinfo->resultcode, errinfo->pc,
                   errinfo->message);

            cmd_params[0] = 0;
            cmd_params[1] = 1;
            break;
        }
        default:
            lwarn("unknown command 0x%04x", cmd.command);
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = -1;
            break;
    }
}