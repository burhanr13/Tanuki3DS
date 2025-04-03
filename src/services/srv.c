#include "srv.h"

#include <string.h>

#include "emulator.h"
#include "kernel/memory.h"
#include "kernel/svc.h"

#include "font.h"
#include "services.h"

struct {
    const char* name;
    PortRequestHandler handler;
} srvhandlers[] = {
#define SRV(portname, name) {portname, port_handle_##name}
    SRV("APT:U", apt),    SRV("APT:A", apt),        SRV("APT:S", apt),
    SRV("fs:USER", fs),   SRV("gsp::Gpu", gsp_gpu), SRV("hid:USER", hid),
    SRV("hid:SPVR", hid), SRV("dsp::DSP", dsp),     SRV("cfg:u", cfg),
    SRV("cfg:s", cfg),    SRV("cfg:i", cfg),        SRV("y2r:u", y2r),
    SRV("cecd:u", cecd),  SRV("ldr:ro", ldr_ro),    SRV("nwm::UDS", nwm_uds),
    SRV("ir:USER", ir),   SRV("am:app", am),
#undef SRV
};

#define SRVCOUNT (sizeof srvhandlers / sizeof srvhandlers[0])

u8 shared_font[] = {
#embed "font.bcfnt"
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

void services_init(E3DS* s) {
    srvobj_init(&s->services.notif_sem.hdr, KOT_SEMAPHORE);
    s->services.notif_sem.count = 0;
    s->services.notif_sem.max = 1;

    srvobj_init(&s->services.apt.lock.hdr, KOT_MUTEX);
    srvobj_init(&s->services.apt.notif_event.hdr, KOT_EVENT);
    s->services.apt.notif_event.sticky = true;
    srvobj_init(&s->services.apt.resume_event.hdr, KOT_EVENT);
    s->services.apt.resume_event.sticky = true;
    srvobj_init(&s->services.apt.shared_font.hdr, KOT_SHAREDMEM);
    s->services.apt.shared_font.size = 0x80 + sizeof shared_font;
    sharedmem_alloc(s, &s->services.apt.shared_font);
    // the shared font is treated as part of the linear heap
    // so its paddr and vaddr must be related appropriately
    s->services.apt.shared_font.mapaddr =
        s->services.apt.shared_font.paddr - FCRAM_PBASE + LINEAR_HEAP_BASE;
    u32* fontdest = PPTR(s->services.apt.shared_font.paddr);
    fontdest[0] = 2;                  // 2 = font loaded
    fontdest[1] = 1;                  // region
    fontdest[2] = sizeof shared_font; // size
    // font is at offset 0x80 of the memory block
    memcpy((void*) fontdest + 0x80, shared_font, sizeof shared_font);
    // now we need to relocate the pointers in the font too (games will do it
    // themselves but homebrew needs it to be already done)
    font_relocate((void*) fontdest + 0x80,
                  s->services.apt.shared_font.mapaddr + 0x80);
    srvobj_init(&s->services.apt.capture_block.hdr, KOT_SHAREDMEM);
    // rgba framebuffers for bottom, top left, top right
    s->services.apt.capture_block.size = 4 * (0x7000 + 2 * 0x19000);
    sharedmem_alloc(s, &s->services.apt.capture_block);

    s->services.gsp.event = nullptr;
    srvobj_init(&s->services.gsp.sharedmem.hdr, KOT_SHAREDMEM);
    s->services.gsp.sharedmem.size = sizeof(GSPSharedMem);
    sharedmem_alloc(s, &s->services.gsp.sharedmem);

    srvobj_init(&s->services.dsp.sem_event.hdr, KOT_EVENT);

    srvobj_init(&s->services.hid.sharedmem.hdr, KOT_SHAREDMEM);
    s->services.hid.sharedmem.size = sizeof(HIDSharedMem);
    sharedmem_alloc(s, &s->services.hid.sharedmem);

    for (int i = 0; i < HIDEVENT_MAX; i++) {
        srvobj_init(&s->services.hid.events[i].hdr, KOT_EVENT);
        s->services.hid.events[i].sticky = true;
    }

    srvobj_init(&s->services.cecd.cecinfo.hdr, KOT_EVENT);

    srvobj_init(&s->services.y2r.transferend.hdr, KOT_EVENT);

    srvobj_init(&s->services.ir.event.hdr, KOT_EVENT);
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

            PortRequestHandler handler = nullptr;
            for (int i = 0; i < SRVCOUNT; i++) {
                if (!strcmp(name, srvhandlers[i].name)) {
                    handler = srvhandlers[i].handler;
                    break;
                }
            }
            u32 handle = handle_new(s);
            KSession* session;
            if (handler) {
                session = session_create(handler);
            } else {
                lerror("unknown service '%.8s'", name);
                session = session_create_arg(port_handle_stub, *(u64*) name);
            }
            HANDLE_SET(handle, session);
            session->hdr.refcount = 1;
            linfo("connected to service '%.8s' with handle %x", name, handle);

            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = handle;
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
            longjmp(ctremu.exceptionJmp, 1);
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