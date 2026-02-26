#include "gsp.h"

#include "3ds.h"
#include "kernel/kernel.h"
#include "scheduler.h"
#include "video/gpu.h"

#define GSPMEM ((GSPSharedMem*) PPTR(s->services.gsp.sharedmem.paddr))

DECL_PORT(gsp_gpu) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0008: {
            linfo("FlushDataCache");
            u32 addr = cmdbuf[1];
            u32 size = cmdbuf[2];
            gpu_invalidate_range(&s->gpu, vaddr_to_paddr(addr), size);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
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
        case 0x0010: {
            linfo("SetAxiConfigQoSMode");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
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
            s->services.gsp.registered = true;
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
        case 0x001f: {
            linfo("StoreDataCache");
            u32 addr = cmdbuf[1];
            u32 size = cmdbuf[2];
            gpu_invalidate_range(&s->gpu, vaddr_to_paddr(addr), size);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
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

void update_fbinfos(E3DS* s) {
    // updates internal state from the fbinfo in shared mem
    // this should happen every vblank and every display transfer
    // applications use multiple framebuffers and swap between them
    // we don't really care and just use whatever one we can and this
    // is kept track of by storing the most recent fbs for a screen in a fifo

    for (int i = 0; i < 2; i++) {
        auto fb = &GSPMEM->fbinfo[0][i];
        if (fb->newdataflag) {
            linfo("%s fb idx %d [0] l %08x r %08x [1] l %08x r %08x",
                  i == SCREEN_TOP ? "top" : "bot", fb->idx,
                  fb->fbs[0].left_vaddr, fb->fbs[0].right_vaddr,
                  fb->fbs[1].left_vaddr, fb->fbs[1].right_vaddr);

            auto fbent = &fb->fbs[1 - fb->idx];
            LCDFBInfo lcdfb = {
                .vaddr = fbent->left_vaddr,
                .fmt = fbent->format,
            };
            FIFO_push(s->services.gsp.lcdfbs[i], lcdfb);
            fb->newdataflag = 0;
        }
    }
}

void gsp_handle_event(E3DS* s, u32 id) {
    if (id == GSPEVENT_VBLANK0) {
        add_event(&s->sched, (SchedulerCallback) gsp_handle_event,
                  (void*) GSPEVENT_VBLANK0, CPU_CLK / FPS);

        gsp_handle_event(s, GSPEVENT_VBLANK1);

        linfo("vblank");

        for (int sc = 0; sc < 2; sc++) {
            bool isSwRender = true;
            for (int i = 0; i < 4; i++) {
                if (s->services.gsp.lcdfbs[sc].d[i].wasDisplayTransferred)
                    isSwRender = false;
            }
            if (!isSwRender) continue;
            auto lastfb = &FIFO_back(s->services.gsp.lcdfbs[sc]);
            if (!lastfb->vaddr) continue;
            gpu_render_lcd_fb(&s->gpu, vaddr_to_paddr(lastfb->vaddr),
                              lastfb->fmt, sc);
        }

        update_fbinfos(s);

        s->frame_complete = true;
    }

    if (!s->services.gsp.registered) return;

    auto interrupts = &GSPMEM->interrupts[0];

    if (interrupts->count < 0x34) {
        u32 idx = interrupts->cur + interrupts->count;
        if (idx >= 0x34) idx -= 0x34;
        interrupts->queue[idx] = id;
        interrupts->count++;
    }

    if (s->services.gsp.event) {
        linfo("signaling gsp event %d", id);
        event_signal(s, s->services.gsp.event);
    }
}

void gsp_handle_command(E3DS* s) {
    auto cmds = &GSPMEM->commands[0];

    if (cmds->count == 0) return;

    switch (cmds->d[cmds->cur].id) {
        case 0x00: {
            u32 src = cmds->d[cmds->cur].args[0];
            u32 dest = cmds->d[cmds->cur].args[1];
            u32 size = cmds->d[cmds->cur].args[2];
            linfo("dma request from %08x to %08x of size 0x%x", src, dest,
                  size);
            memcpy(PTR(dest), PTR(src), size);
            gpu_invalidate_range(&s->gpu, vaddr_to_paddr(dest), size);
            gsp_handle_event(s, GSPEVENT_DMA);
            break;
        }
        case 0x01: {
            u32 bufaddr = cmds->d[cmds->cur].args[0];
            u32 bufsize = cmds->d[cmds->cur].args[1];
            linfo("sending command list at %08x with size 0x%x", bufaddr,
                  bufsize);
            gpu_reset_needs_rehesh(&s->gpu);
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
                if (!cmd->buf[i].st) continue;
                linfo("memory fill at fb %08x-%08x with %x", cmd->buf[i].st,
                      cmd->buf[i].end, cmd->buf[i].val);
                gpu_clear_fb(&s->gpu, vaddr_to_paddr(cmd->buf[i].st),
                             vaddr_to_paddr(cmd->buf[i].end), cmd->buf[i].val,
                             (cmd->ctl[i] >> 8) + 2);
                gsp_handle_event(s, GSPEVENT_PSC0 + i);
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
            [[maybe_unused]] u8 fmtin = (flags >> 8) & 7;
            u8 fmtout = (flags >> 12) & 7;
            u8 scalemode = (flags >> 24) & 3;
            bool scalex = scalemode >= 1;
            bool scaley = scalemode >= 2;
            bool vflip =
                flags & BIT(0); // need to handle yoff differently probably
            bool lineartotiled = flags & BIT(1);

            // these values are the unscaled dims for some reason
            if (scalex) wout /= 2;
            if (scaley) hout /= 2;

            static int fmtBpp[8] = {4, 3, 2, 2, 2, 4, 4, 4};

            linfo("display transfer from fb at %08x (%dx%d) to %08x (%dx%d) "
                  "flags %x",
                  addrin, win, hin, addrout, wout, hout, flags);

            update_fbinfos(s);

            bool found = false;
            for (int screen = 0; screen < 2; screen++) {
                for (int i = 0; i < 4; i++) {
                    u32 yoff =
                        s->services.gsp.lcdfbs[screen].d[i].vaddr - addrout;
                    yoff /= wout * fmtBpp[fmtout];
                    if (yoff < hout) {
                        gpu_display_transfer(&s->gpu, vaddr_to_paddr(addrin),
                                             yoff, scalex, scaley, vflip,
                                             screen);
                        s->services.gsp.lcdfbs[screen]
                            .d[i]
                            .wasDisplayTransferred = true;
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                // scuffed sw display transfer for virtual console
                if (lineartotiled) {
                    lwarnonce("linear to tiled display transfer fmt %d",
                              fmtout);
                    u16* data = PTR(addrout);
                    u16* pixels = PTR(addrin);
                    for (int y = 0; y < hout; y++) {
                        for (int x = 0; x < wout; x++) {
                            data[morton_swizzle(wout, x, y)] =
                                pixels[(vflip ? hout - 1 - y : y) * wout + x];
                        }
                    }
                    gpu_invalidate_range(&s->gpu, vaddr_to_paddr(addrout),
                                         wout * hout * fmtBpp[fmtout]);
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

            pitchin <<= 4;
            gapin <<= 4;
            pitchout <<= 4;
            gapout <<= 4;

            linfo("texture copy from %x(pitch=%d,gap=%d) to "
                  "%x(pitch=%d,gap=%d), size=%d, flags=%x",
                  addrin, pitchin, gapin, addrout, pitchout, gapout, copysize,
                  flags);

            gpu_texture_copy(&s->gpu, vaddr_to_paddr(addrin),
                             vaddr_to_paddr(addrout), copysize, pitchin, gapin,
                             pitchout, gapout);

            gsp_handle_event(s, GSPEVENT_PPF);
            break;
        }
        case 0x05: {
            linfo("flush cache regions");
            struct {
                u32 id;
                struct {
                    u32 addr;
                    u32 size;
                } buf[3];
                u32 unused;
            }* cmd = (void*) &cmds->d[cmds->cur];
            for (int i = 0; i < 3; i++) {
                if (cmd->buf[i].size == 0) break;
                gpu_invalidate_range(&s->gpu, vaddr_to_paddr(cmd->buf[i].addr),
                                     cmd->buf[i].size);
            }
            break;
        }
        default:
            lwarn("unknown gsp queue command 0x%02x", cmds->d[cmds->cur].id);
    }

    cmds->cur++;
    if (cmds->cur >= 15) cmds->cur -= 15;
    cmds->count--;
}
