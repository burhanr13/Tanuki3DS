#include "gsp.h"

#include "../3ds.h"
#include "../kernel.h"
#include "../pica/gpu.h"
#include "../scheduler.h"

#define GSPMEM(off) PTR(s->services.gsp.sharedmem.vaddr + off)

DECL_PORT(gsp_gpu) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0008:
            linfo("FlushDataCache");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x000b:
            linfo("LCDForceBlank");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x000c:
            linfo("TriggerCmdReqQueue");
            gsp_handle_command(s);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0013: {
            cmdbuf[0] = IPCHDR(2, 2);

            u32 shmemhandle =
                srvobj_make_handle(s, &s->services.gsp.sharedmem.hdr);
            if (!shmemhandle) {
                cmdbuf[1] = -1;
                return;
            }

            KEvent* event = HANDLE_GET_TYPED(cmdbuf[3], KOT_EVENT);
            if (!event) {
                lerror("invalid event handle");
                cmdbuf[1] = -1;
                return;
            }

            s->services.gsp.event = event;
            event->hdr.refcount++;
            event->sticky = true;

            linfo("RegisterInterruptRelayQueue with event handle=%x, "
                  "shmemhandle=%x",
                  cmdbuf[3], shmemhandle);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = 0;
            cmdbuf[4] = shmemhandle;
            break;
        }
        case 0x0016:
            linfo("AcquireRight");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0018:
            linfo("ImportDisplayCaptureInfo");
            cmdbuf[0] = IPCHDR(9, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = LINEAR_HEAP_BASE;
            cmdbuf[3] = LINEAR_HEAP_BASE;
            cmdbuf[4] = 0;
            cmdbuf[5] = 0;
            cmdbuf[6] = LINEAR_HEAP_BASE;
            cmdbuf[7] = 0;
            cmdbuf[8] = 0;
            cmdbuf[9] = 0;
            break;
        case 0x001e:
            linfo("SetInternalPriorities");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x001f:
            linfo("StoreDataCache");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}

void gsp_handle_event(HLE3DS* s, u32 arg) {
    if (arg == GSPEVENT_VBLANK0) {
        add_event(&s->sched, gsp_handle_event, GSPEVENT_VBLANK0, CPU_CLK / FPS);

        gsp_handle_event(s, GSPEVENT_VBLANK1);
        if (s->services.dsp.event) event_signal(s, s->services.dsp.event);

        linfo("vblank");

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
            u32 src = cmds->d[cmds->cur].args[0];
            u32 dest = cmds->d[cmds->cur].args[1];
            u32 size = cmds->d[cmds->cur].args[2];
            linfo("dma request from %08x to %08x of size 0x%x", src, dest,
                  size);
            memcpy(PTR(dest), PTR(src), size);
            gsp_handle_event(s, GSPEVENT_DMA);
            break;
        }
        case 0x01: {
            u32 bufaddr = cmds->d[cmds->cur].args[0];
            u32 bufsize = cmds->d[cmds->cur].args[1];
            linfo("sending command list at %08x with size 0x%x", bufaddr,
                  bufsize);
            gpu_run_command_list(&s->gpu, vaddr_to_paddr(bufaddr & ~7),
                                 bufsize);
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
                    gpu_clear_fb(&s->gpu, vaddr_to_paddr(cmd->buf[i].st),
                                 cmd->buf[i].val);
                    gsp_handle_event(s, GSPEVENT_PSC0 + i);
                }
            }
            break;
        }
        case 0x03: {
            u32 addrin = cmds->d[cmds->cur].args[0];
            u32 addrout = cmds->d[cmds->cur].args[1];
            u32 dimin = cmds->d[cmds->cur].args[2];
            u16 win = dimin & 0xffff;
            u16 hin = dimin >> 16;
            u32 dimout = cmds->d[cmds->cur].args[3];
            u16 wout = dimout & 0xffff;
            u16 hout = dimout >> 16;
            u32 flags = cmds->d[cmds->cur].args[4];
            u8 fmtin = (flags >> 8) & 7;
            u8 fmtout = (flags >> 12) & 7;
            u8 scalemode = (flags >> 24) & 3;
            bool scalex = scalemode >= 1;
            bool scaley = scalemode >= 2;

            static int fmtBpp[8] = {4, 3, 2, 2, 2, 4, 4, 4};

            linfo("display transfer from fb at %08x (%dx%d) to %08x (%dx%d) "
                  "flags %x",
                  addrin, win, hin, addrout, wout, hout, flags);

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

            if (fbtop->flags & 1) {
                linfo("top fb idx %d [0] l %08x r %08x [1] l %08x r %08x",
                      fbtop->idx, fbtop->fbs[0].left_vaddr,
                      fbtop->fbs[0].right_vaddr, fbtop->fbs[1].left_vaddr,
                      fbtop->fbs[1].right_vaddr);

                FIFO_push(s->services.gsp.toplcdfbs,
                          fbtop->fbs[1 - fbtop->idx].left_vaddr);
                fbtop->flags &= ~1;
            }
            if (fbbot->flags & 1) {
                linfo("bot fb idx %d [0] l %08x r %08x [1] l %08x r %08x",
                      fbbot->idx, fbbot->fbs[0].left_vaddr,
                      fbbot->fbs[0].right_vaddr, fbbot->fbs[1].left_vaddr,
                      fbbot->fbs[1].right_vaddr);

                FIFO_push(s->services.gsp.botlcdfbs,
                          fbbot->fbs[1 - fbbot->idx].left_vaddr);
                fbbot->flags &= ~1;
            }

            for (int i = 0; i < 4; i++) {
                int yoff = addrout - s->services.gsp.toplcdfbs.d[i];
                yoff /= wout * fmtBpp[fmtout];
                if (abs(yoff) < hout / 2) {
                    gpu_display_transfer(&s->gpu, vaddr_to_paddr(addrin), yoff, 
                                         scalex, scaley, true);
                    break;
                }
            }
            for (int i = 0; i < 4; i++) {
                int yoff = addrout - s->services.gsp.botlcdfbs.d[i];
                yoff /= wout * fmtBpp[fmtout];
                if (abs(yoff) < hout / 2) {
                    gpu_display_transfer(&s->gpu, vaddr_to_paddr(addrin), yoff,
                                         scalex, scaley, false);
                    break;
                }
            }

            gsp_handle_event(s, GSPEVENT_PPF);
            break;
        }
        case 0x04: {
            u32 addrin = cmds->d[cmds->cur].args[0];
            u32 addrout = cmds->d[cmds->cur].args[1];
            u32 copysize = cmds->d[cmds->cur].args[2];
            u32 pitchin = cmds->d[cmds->cur].args[3] & 0xffff;
            u32 gapin = cmds->d[cmds->cur].args[3] >> 16;
            u32 pitchout = cmds->d[cmds->cur].args[4] & 0xffff;
            u32 gapout = cmds->d[cmds->cur].args[4] >> 16;
            u32 flags = cmds->d[cmds->cur].args[5];

            linfo("texture copy from %x(pitch=%d,gap=%d) to "
                  "%x(pitch=%d,gap=%d), size=%d, flags=%x",
                  addrin, pitchin, gapin, addrout, pitchout, gapout, copysize,
                  flags);

            u8* src = PTR(addrin);
            u8* dst = PTR(addrout);

            int cnt = 0;
            while (cnt < copysize) {
                memcpy(dst, src, pitchin);
                cnt += pitchin;
                src += pitchin + gapin;
                dst += pitchout + gapout;
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