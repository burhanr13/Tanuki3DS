#include "dsp.h"

#include "3ds.h"
#include "emulator.h"

// lle dsp code is based directly on citra

u16 dsp_addrs[15] = {
    0xBFFF, 0x9E92, 0x8680, 0xA792, 0x9430, 0x8400, 0x8540, 0x9492,
    0x8710, 0x8410, 0xA912, 0xAA12, 0xAAD2, 0xAC52, 0xAC5C,
};

DECL_PORT(dsp) {
    u32* cmdbuf = PTR(cmd_addr);

    auto teakra = s->services.dsp.teakra;
    switch (cmd.command) {
        case 0x0001: {
            int reg = cmdbuf[1];
            linfo("RecvData %d", reg);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 1;
            break;
        }
        case 0x0002:
            linfo("RecvDataIsReady");
            // stub
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 1;
            break;
        case 0x0007:
            linfo("SetSemaphore %x", cmdbuf[1]);
            if (teakra) Teakra_SetSemaphore(teakra, cmdbuf[1]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x000c: {
            linfo("ConvertProcessAddressFromDspDram");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[2] = DSPRAM_VBASE + DSPRAM_DATA_OFF + (cmdbuf[1] << 1);
            cmdbuf[1] = 0;
            break;
        }
        case 0x000d: {
            u32 chan = cmdbuf[1];
            u32 size = cmdbuf[2];
            void* buf = PTR(cmdbuf[4]);
            linfo("WriteProcessPipe ch=%d, sz=%d", chan, size);
            if (teakra) dsp_lle_write_pipe(s, chan, buf, size);
            break;
        }
        case 0x0010: {
            u32 chan = cmdbuf[1];
            u32 size = (u16) cmdbuf[3];
            void* buf = PTR(cmdbuf[0x41]);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = size;

            linfo("ReadPipeIfPossible chan=%d with size 0x%x", chan, size);

            if (teakra) {
                dsp_lle_read_pipe(s, chan, buf, size);
            } else {
                // the dsp code first reads 2 bytes containing the number of
                // entries (15) then it reads 15 shorts (30 bytes) containing
                // dsp addresses of each dsp firmware memory region

                if (size == 2) {
                    *(u16*) buf = 15;
                }
                if (size == 30) {
                    memcpy(buf, dsp_addrs, sizeof dsp_addrs);
                }

                // but you can also read it all at once
                if (size == 32) {
                    *(u16*) buf = 15;
                    memcpy(buf + 2, dsp_addrs, sizeof dsp_addrs);
                }
            }

            break;
        }
        case 0x0011: {
            linfo("LoadComponent");
            u32 size = cmdbuf[1];
            void* buf = PTR(cmdbuf[5]);

            if (s->services.dsp.teakra) dsp_lle_load_component(s, buf);

            cmdbuf[0] = IPCHDR(2, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = true;
            cmdbuf[3] = cmdbuf[4];
            cmdbuf[4] = cmdbuf[5];
            break;
        }
        case 0x0013:
            linfo("FlushDataCache");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0014:
            linfo("InvalidateDCache");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0015: {
            int interrupt = cmdbuf[1];
            int channel = cmdbuf[2];
            linfo("RegisterInterruptEvents int=%d,ch=%d with handle %x",
                  interrupt, channel, cmdbuf[4]);
            if (interrupt >= 3 || channel >= 4) {
                lerror("invalid channel");
                break;
            }

            KEvent** event = &s->services.dsp.events[interrupt][channel];

            // unregister an existing event
            if (*event) {
                (*event)->hdr.refcount--;
                *event = nullptr;
            }

            *event = HANDLE_GET_TYPED(cmdbuf[4], KOT_EVENT);
            if (*event) {
                (*event)->hdr.refcount++;
            }

            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0016:
            linfo("GetSemaphoreEventHandle");
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.dsp.sem_event.hdr);
            break;
        case 0x0017:
            linfo("SetSemaphoreMask %x", cmdbuf[1]);
            s->services.dsp.sem_mask = cmdbuf[1];
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x001f:
            linfo("GetHeadphoneStatus");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = false;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}

#define DSPPTR(addr) PPTR(DSPRAM_PBASE + DSPRAM_DATA_OFF + (addr << 1))

enum {
    PIPE_TOCPU,
    PIPE_TODSP,
};

typedef struct {
    u16 addr;
    u16 size;
    // these are 16 bit but the actual address is only the bottom 15 bits
    u16 read_ptr;
    u16 write_ptr;
    u8 slot;
    u8 flags;
} Pipe;

u8 dsp_read8(E3DS* s, u32 addr) {
    return *(u8*) PPTR(addr);
}
u16 dsp_read16(E3DS* s, u32 addr) {
    return *(u16*) PPTR(addr);
}
u32 dsp_read32(E3DS* s, u32 addr) {
    return *(u32*) PPTR(addr);
}
void dsp_write8(E3DS* s, u32 addr, u8 data) {
    *(u8*) PPTR(addr) = data;
}
void dsp_write16(E3DS* s, u32 addr, u16 data) {
    *(u16*) PPTR(addr) = data;
}
void dsp_write32(E3DS* s, u32 addr, u32 data) {
    *(u32*) PPTR(addr) = data;
}

void sem_event_handler(E3DS* s) {
    linfo("sem event signaled");
    auto dsp = &s->services.dsp;
    Teakra_SetSemaphore(dsp->teakra, dsp->sem_mask);
}

void recv_data_0_handler(E3DS* s) {
    linfo("recv data on ch0");
    if (s->services.dsp.events[0][0]) {
        event_signal(s, s->services.dsp.events[0][0]);
    }
}

void recv_data_1_handler(E3DS* s) {
    linfo("recv data on ch1");
    if (s->services.dsp.events[1][0]) {
        event_signal(s, s->services.dsp.events[1][0]);
    }
}
static int bruh = 0;
void handle_pipe_event(E3DS* s) {
    if (s->services.dsp.data_signaled && s->services.dsp.sem_signaled) {
        s->services.dsp.data_signaled = s->services.dsp.sem_signaled = false;
        u16 slot = Teakra_RecvData(s->services.dsp.teakra, 2);
        u16 dir = slot & 1;
        if (dir == PIPE_TODSP) return;
        u16 pipe = slot >> 1;
        linfo("dsp wrote pipe %d %d", pipe, bruh++);
        if (pipe == 0) {
            lwarn("debug pipe");
            return;
        }
        if (pipe < 4 && s->services.dsp.events[2][pipe]) {
            event_signal(s, s->services.dsp.events[2][pipe]);
        }
    }
}

void recv_data_2_handler(E3DS* s) {
    linfo("recv data on ch2");
    // ignore until the initialization is complete
    if (!s->services.dsp.component_loaded) return;
    s->services.dsp.data_signaled = true;
    handle_pipe_event(s);
}

void semaphore_handler(E3DS* s) {
    linfo("dsp semaphore signaled");
    // ignore until the initialization is complete
    if (!s->services.dsp.component_loaded) return;
    if (!(Teakra_GetSemaphore(s->services.dsp.teakra) & 0x8000)) return;
    s->services.dsp.sem_signaled = true;
    handle_pipe_event(s);
}

void audio_callback(void*, s16 samples[2]) {
    ctremu.audio_cb(samples);
}

void dsp_lle_init(E3DS* s) {
    auto teakra = Teakra_Create();
    Teakra_SetDspMemory(teakra, PPTR(DSPRAM_PBASE));
    Teakra_SetAHBMCallback(teakra, (Teakra_AHBMReadCallback8) dsp_read8,
                           (Teakra_AHBMWriteCallback8) dsp_write8,
                           (Teakra_AHBMReadCallback16) dsp_read16,
                           (Teakra_AHBMWriteCallback16) dsp_write16,
                           (Teakra_AHBMReadCallback32) dsp_read32,
                           (Teakra_AHBMWriteCallback32) dsp_write32, s);
    Teakra_SetRecvDataHandler(
        teakra, 0, (Teakra_InterruptCallback) recv_data_0_handler, s);
    Teakra_SetRecvDataHandler(
        teakra, 1, (Teakra_InterruptCallback) recv_data_1_handler, s);
    Teakra_SetRecvDataHandler(
        teakra, 2, (Teakra_InterruptCallback) recv_data_2_handler, s);
    Teakra_SetSemaphoreHandler(teakra,
                               (Teakra_InterruptCallback) semaphore_handler, s);
    Teakra_SetAudioCallback(teakra, audio_callback, nullptr);

    s->services.dsp.teakra = teakra;

    s->services.dsp.sem_event.callback = sem_event_handler;
}

void dsp_lle_destroy(E3DS* s) {
    Teakra_Destroy(s->services.dsp.teakra);
}

void dsp_lle_run_slice(E3DS* s) {
    // dsp is clocked at half the speed of cpu
    Teakra_Run(s->services.dsp.teakra, DSP_SLICE_CYCLES / 2);
}

void dsp_lle_run_event(E3DS* s, u32) {
    dsp_lle_run_slice(s);
    add_event(&s->sched, dsp_lle_run_event, 0, DSP_SLICE_CYCLES);
}

void dsp_lle_read_pipe(E3DS* s, u32 index, u8* dst, u32 len) {
    ldebug("reading pipe %d size %d", index, len);

    Pipe* pipes = DSPPTR(s->services.dsp.pipe_addr);
    int slot = index << 1 | PIPE_TOCPU;
    u8* pipedata = DSPPTR(pipes[slot].addr);
    for (int i = 0; i < len; i++) {
        dst[i] = pipedata[pipes[slot].read_ptr & ~BIT(15)];
        pipes[slot].read_ptr++;
    }
    pipes[slot].size -= len;
    // notify dsp that pipe was read
    Teakra_SendData(s->services.dsp.teakra, 2, slot);
}

void dsp_lle_write_pipe(E3DS* s, u32 index, u8* src, u32 len) {
    ldebug("writing pipe %d size %d", index, len);

    Pipe* pipes = DSPPTR(s->services.dsp.pipe_addr);
    int slot = index << 1 | PIPE_TODSP;
    u8* pipedata = DSPPTR(pipes[slot].addr);
    for (int i = 0; i < len; i++) {
        pipedata[pipes[slot].write_ptr & ~BIT(15)] = src[i];
        pipes[slot].write_ptr++;
    }
    pipes[slot].size += len;
    // notify dsp that pipe was written
    Teakra_SendData(s->services.dsp.teakra, 2, slot);
}

void dsp_lle_load_component(E3DS* s, void* buf) {
    if (s->services.dsp.component_loaded) {
        lwarn("component already loaded");
        return;
    }

    Teakra_Reset(s->services.dsp.teakra);

    DSP1Header* hdr = buf;
    for (int i = 0; i < hdr->num_segments; i++) {
        void* src = buf + hdr->segs[i].offset;
        u32 base = DSPRAM_VBASE;
        if (hdr->segs[i].type == 2) base += DSPRAM_DATA_OFF;
        void* dst = PTR(base + (hdr->segs[i].dspaddr << 1));
        memcpy(dst, src, hdr->segs[i].size);
        linfo("loaded segment from offset %x to %08x[%x] with size %x",
              hdr->segs[i].offset, base, hdr->segs[i].dspaddr,
              hdr->segs[i].size);
    }

    // wait until each channel has sent 1
    if (hdr->recv_data) {
        for (int i = 0; i < 3; i++) {
            while (true) {
                while (!Teakra_RecvDataIsReady(s->services.dsp.teakra, i)) {
                    dsp_lle_run_slice(s);
                }
                if (Teakra_RecvData(s->services.dsp.teakra, i) == 1) break;
            }
            linfo("received 1 on channel %d", i);
        }
    }

    // get pipe addr
    while (!Teakra_RecvDataIsReady(s->services.dsp.teakra, 2)) {
        dsp_lle_run_slice(s);
    }
    s->services.dsp.pipe_addr = Teakra_RecvData(s->services.dsp.teakra, 2);
    linfo("loaded dsp component, pipe addr is %04x", s->services.dsp.pipe_addr);
    // finally schedule dsp to keep running
    add_event(&s->sched, dsp_lle_run_event, 0, DSP_SLICE_CYCLES);

    s->services.dsp.component_loaded = true;
}