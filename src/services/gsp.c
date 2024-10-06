#include "gsp.h"

#include "../kernel.h"
#include "../pica/gpu.h"
#include "../scheduler.h"
#include "../svc.h"

#define GSPMEM(off) PTR(s->services.gsp.sharedmem.vaddr + off)

DECL_PORT(gsp_gpu) {
    u32* cmd_params = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0008:
            linfo("FlushDataCache");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x000b:
            linfo("LCDForceBlank");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x000c:
            linfo("TriggerCmdReqQueue");
            gsp_handle_command(s);
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x0013: {
            cmd_params[0] = MAKE_IPCHEADER(2, 2);

            u32 shmemhandle =
                srvobj_make_handle(s, &s->services.gsp.sharedmem.hdr);
            if (!shmemhandle) {
                cmd_params[1] = -1;
                return;
            }

            KEvent* event = HANDLE_GET_TYPED(cmd_params[3], KOT_EVENT);
            if (!event) {
                lerror("invalid event handle");
                cmd_params[1] = -1;
                return;
            }

            s->services.gsp.event = event;
            event->hdr.refcount++;
            event->sticky = true;

            linfo("RegisterInterruptRelayQueue with event handle=%x, "
                  "shmemhandle=%x",
                  cmd_params[3], shmemhandle);
            cmd_params[1] = 0;
            cmd_params[2] = 0;
            cmd_params[3] = 0;
            cmd_params[4] = shmemhandle;
            break;
        }
        case 0x0016:
            linfo("AcquireRight");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x001e:
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x", cmd.command);
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = -1;
            break;
    }
}

void gsp_handle_event(HLE3DS* s, u32 arg) {
    if (arg == GSPEVENT_VBLANK0) {
        add_event(&s->sched, gsp_handle_event, GSPEVENT_VBLANK0, CPU_CLK / FPS);

        gsp_handle_event(s, GSPEVENT_VBLANK1);
        if (s->services.dsp.event) event_signal(s, s->services.dsp.event);

        linfo("vblank");

        s->gpu.cur_fb = -1;
        
        s->frame_complete = true;
    }

    if (!s->services.gsp.sharedmem.mapped) return;

    struct {
        u8 cur;
        u8 count;
        u8 err;
        u8 flags;
        u8 errvblank0[4];
        u8 errvblank1[4];
        u8 queue[0x34];
    }* interrupts = GSPMEM(0);

    if (interrupts->count < 0x34) {
        u32 idx = interrupts->cur + interrupts->count;
        if (idx >= 0x34) idx -= 0x34;
        interrupts->queue[idx] = arg;
        interrupts->count++;
    }

    if (s->services.gsp.event) {
        linfo("signaling gsp event %d", arg);
        event_signal(s, s->services.gsp.event);
    }
}

u32 vaddr_to_paddr(u32 vaddr) {
    if (vaddr >= VRAMBASE) {
        return vaddr - VRAMBASE + VRAM_PBASE;
    }
    return vaddr - LINEAR_HEAP_BASE + FCRAM_PBASE;
}

void gsp_handle_command(HLE3DS* s) {
    struct {
        u8 cur;
        u8 count;
        u8 flags;
        u8 flags2;
        u8 err;
        u8 pad[27];
        struct {
            u8 id;
            u8 unk;
            u8 unk2;
            u8 mode;
            u32 args[7];
        } d[15];
    }* cmds = GSPMEM(0x800);

    if (cmds->count == 0) return;

    switch (cmds->d[cmds->cur].id) {
        case 0x00: {
            linfo("dma request");
            gsp_handle_event(s, GSPEVENT_DMA);
            break;
        }
        case 0x01: {
            u32 bufaddr = cmds->d[cmds->cur].args[0];
            u32 bufsize = cmds->d[cmds->cur].args[1];
            linfo("sending command list at %08x with size 0x%x", bufaddr,
                  bufsize);
            gpu_run_command_list(&s->gpu, PTR(bufaddr & ~7), bufsize / 4);
            gsp_handle_event(s, GSPEVENT_P3D);
            break;
        }
        case 0x02: {
            struct {
                u32 id;
                struct {
                    u32 st;
                    u32 val;
                    u32 end;
                } buf[2];
                u16 ctl[2];
            }* cmd = (void*) &cmds->d[cmds->cur];
            for (int i = 0; i < 2; i++) {
                if (cmd->buf[i].st) {
                    linfo("memory fill at fb %08x-%08x with %x", cmd->buf[i].st,
                          cmd->buf[i].end, cmd->buf[i].val);
                    gpu_set_fb_cur(&s->gpu, vaddr_to_paddr(cmd->buf[i].st));
                    gpu_clear_fb(&s->gpu, cmd->buf[i].val);
                    gsp_handle_event(s, GSPEVENT_PSC0 + i);
                }
            }
            break;
        }
        case 0x03: {
            u32 addrin = cmds->d[cmds->cur].args[0];
            u32 addrout = cmds->d[cmds->cur].args[1];
            u32 dimin = cmds->d[cmds->cur].args[2];
            u32 dimout = cmds->d[cmds->cur].args[3];
            u32 flags = cmds->d[cmds->cur].args[4];
            linfo("display transfer from fb at %08x to %08x flags %x", addrin,
                  addrout, flags);

            struct {
                u8 idx;
                u8 flags;
                u16 _pad;
                struct {
                    u32 active;
                    u32 left_vaddr;
                    u32 right_vaddr;
                    u32 stride;
                    u32 format;
                    u32 status;
                    u32 unk;
                } fbs[2];
            } *fbtop = GSPMEM(0x200), *fbbot = GSPMEM(0x240);

            linfo("top fb idx %d [0] l %08x r %08x [1] l %08x r %08x",
                  fbtop->idx, fbtop->fbs[0].left_vaddr,
                  fbtop->fbs[0].right_vaddr, fbtop->fbs[1].left_vaddr,
                  fbtop->fbs[1].right_vaddr);
            linfo("bot fb idx %d [0] l %08x r %08x [1] l %08x r %08x",
                  fbbot->idx, fbbot->fbs[0].left_vaddr,
                  fbbot->fbs[0].right_vaddr, fbbot->fbs[1].left_vaddr,
                  fbbot->fbs[1].right_vaddr);

            if (addrout == fbtop->fbs[1 - fbtop->idx].left_vaddr) {
                gpu_set_fb_top(&s->gpu, vaddr_to_paddr(addrin));
            } else if (addrout == fbbot->fbs[1 - fbbot->idx].left_vaddr) {
                gpu_set_fb_bot(&s->gpu, vaddr_to_paddr(addrin));
            }

            gsp_handle_event(s, GSPEVENT_PPF);
            break;
        }
        case 0x05:
            linfo("flush cache regions");
            break;
        default:
            lwarn("unknown gsp queue command 0x%02x", cmds->d[cmds->cur].id);
    }

    cmds->cur++;
    if (cmds->cur >= 15) cmds->cur -= 15;
    cmds->count--;
}