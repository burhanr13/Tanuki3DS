#include "dsp_lle.h"

#include "dspptr.inc"

#define DSPPROGPTR(addr) ((void*) &Teakra_GetDspMemory(dsp->teakra)[addr << 1])
#define DSPPTR(addr)                                                           \
    ((void*) &Teakra_GetDspMemory(dsp->teakra)[DSPRAM_DATA_OFF + (addr << 1)])

enum {
    PIPE_TOCPU,
    PIPE_TODSP,
};

typedef struct {
    u16 addr;
    u16 size; // max size of the pipe
    // wrap is copied from ptr & size
    struct {
        u16 ptr : 15;
        u16 wrap : 1;
    } head, tail;
    u8 slot;
    u8 flags;
} Pipe;

u8 dsp_read8(DSP* dsp, u32 addr) {
    return *(u8*) PTR(addr);
}
u16 dsp_read16(DSP* dsp, u32 addr) {
    return *(u16*) PTR(addr);
}
u32 dsp_read32(DSP* dsp, u32 addr) {
    return *(u32*) PTR(addr);
}
void dsp_write8(DSP* dsp, u32 addr, u8 data) {
    *(u8*) PTR(addr) = data;
}
void dsp_write16(DSP* dsp, u32 addr, u16 data) {
    *(u16*) PTR(addr) = data;
}
void dsp_write32(DSP* dsp, u32 addr, u32 data) {
    *(u32*) PTR(addr) = data;
}

void handle_pipe_event(DSP* dsp) {
    if (dsp->data_signaled && dsp->sem_signaled) {
        dsp->data_signaled = dsp->sem_signaled = false;
        u16 slot = Teakra_RecvData(dsp->teakra, 2);
        u16 dir = slot & 1;
        if (dir == PIPE_TODSP) return;
        u16 pipe = slot >> 1;
        linfo("dsp wrote pipe %d", pipe);
        dsp->pipe_written = true;
    }
}

void recv_data_2_handler(DSP* dsp) {
    linfo("recv data on ch2");
    // ignore until the initialization is complete
    if (!dsp->loaded) return;
    dsp->data_signaled = true;
    handle_pipe_event(dsp);
}

void semaphore_handler(DSP* dsp) {
    linfo("dsp semaphore signaled");
    // ignore until the initialization is complete
    if (!dsp->loaded) return;
    if (!(Teakra_GetSemaphore(dsp->teakra) & 0x8000)) return;
    dsp->sem_signaled = true;
    handle_pipe_event(dsp);
}

void dsp_lle_init(DSP* dsp) {
    auto teakra = Teakra_Create();
    Teakra_SetAHBMCallback(teakra, (Teakra_AHBMReadCallback8) dsp_read8,
                           (Teakra_AHBMWriteCallback8) dsp_write8,
                           (Teakra_AHBMReadCallback16) dsp_read16,
                           (Teakra_AHBMWriteCallback16) dsp_write16,
                           (Teakra_AHBMReadCallback32) dsp_read32,
                           (Teakra_AHBMWriteCallback32) dsp_write32, dsp);
    Teakra_SetRecvDataHandler(
        teakra, 2, (Teakra_InterruptCallback) recv_data_2_handler, dsp);
    Teakra_SetSemaphoreHandler(
        teakra, (Teakra_InterruptCallback) semaphore_handler, dsp);

    dsp->teakra = teakra;
}

void dsp_lle_destroy(DSP* dsp) {
    Teakra_Destroy(dsp->teakra);
}

void dsp_lle_run_slice(DSP* dsp) {
    Teakra_Run(dsp->teakra, DSP_SLICE_CYCLES);
}

void dsp_lle_read_pipe(DSP* dsp, u32 index, u8* dst, u32 len) {
    linfo("reading pipe %d size %d", index, len);

    Pipe* pipes = DSPPTR(dsp->pipe_addr);
    int slot = index << 1 | PIPE_TOCPU;
    u8* pipedata = DSPPTR(pipes[slot].addr);

    // move wrap bit to size to make our life easier
    u16 hdptr =
        pipes[slot].head.ptr | (pipes[slot].size * pipes[slot].head.wrap);
    u16 tlptr =
        pipes[slot].tail.ptr | (pipes[slot].size * pipes[slot].tail.wrap);

    // check pipe size
    u16 cursize = (tlptr - hdptr) & (2 * pipes[slot].size - 1);
    if (cursize > pipes[slot].size) lerror("pipe is corrupted");
    if (len > cursize) {
        lerror("not enough data in the pipe");
    }

    for (int i = 0; i < len; i++) {
        dst[i] = pipedata[hdptr & (pipes[slot].size - 1)];
        hdptr++;
    }

    // restore pointers and wrap bit
    pipes[slot].head.ptr = hdptr & (pipes[slot].size - 1);
    pipes[slot].head.wrap = (hdptr & pipes[slot].size) != 0;
    pipes[slot].tail.ptr = tlptr & (pipes[slot].size - 1);
    pipes[slot].tail.wrap = (tlptr & pipes[slot].size) != 0;

    // notify dsp that pipe was read
    Teakra_SendData(dsp->teakra, 2, slot);
}

void dsp_lle_write_pipe(DSP* dsp, u32 index, u8* src, u32 len) {
    linfo("writing pipe %d size %d", index, len);

    Pipe* pipes = DSPPTR(dsp->pipe_addr);
    int slot = index << 1 | PIPE_TODSP;
    u8* pipedata = DSPPTR(pipes[slot].addr);

    // move wrap bit to size to make our life easier
    u16 hdptr =
        pipes[slot].head.ptr | (pipes[slot].size * pipes[slot].head.wrap);
    u16 tlptr =
        pipes[slot].tail.ptr | (pipes[slot].size * pipes[slot].tail.wrap);

    // check pipe size
    u16 cursize = (tlptr - hdptr) & (2 * pipes[slot].size - 1);
    if (cursize > pipes[slot].size) lerror("pipe is corrupted");
    if (cursize + len > pipes[slot].size) {
        lerror("not enough room in the pipe");
    }

    for (int i = 0; i < len; i++) {
        pipedata[tlptr & (pipes[slot].size - 1)] = src[i];
        tlptr++;
    }

    // restore pointers and wrap bit
    pipes[slot].head.ptr = hdptr & (pipes[slot].size - 1);
    pipes[slot].head.wrap = (hdptr & pipes[slot].size) != 0;
    pipes[slot].tail.ptr = tlptr & (pipes[slot].size - 1);
    pipes[slot].tail.wrap = (tlptr & pipes[slot].size) != 0;

    // notify dsp that pipe was written
    Teakra_SendData(dsp->teakra, 2, slot);
}

void dsp_lle_load_component(DSP* dsp, void* buf) {

    Teakra_Reset(dsp->teakra);

    DSP1Header* hdr = buf;
    for (int i = 0; i < hdr->num_segments; i++) {
        void* src = buf + hdr->segs[i].offset;
        u32 base = (hdr->segs[i].type == 2) ? DSPRAM_DATA_OFF : 0;
        void* dst = (hdr->segs[i].type == 2) ? DSPPTR(hdr->segs[i].dspaddr)
                                             : DSPPROGPTR(hdr->segs[i].dspaddr);
        memcpy(dst, src, hdr->segs[i].size);
        linfo("loaded segment from offset %x to %08x[%x] with size %x",
              hdr->segs[i].offset, base, hdr->segs[i].dspaddr,
              hdr->segs[i].size);
    }

    // wait until each channel has sent 1
    if (hdr->recv_data) {
        for (int i = 0; i < 3; i++) {
            while (true) {
                while (!Teakra_RecvDataIsReady(dsp->teakra, i)) {
                    dsp_lle_run_slice(dsp);
                }
                if (Teakra_RecvData(dsp->teakra, i) == 1) break;
            }
            linfo("received 1 on channel %d", i);
        }
    }

    // get pipe addr
    while (!Teakra_RecvDataIsReady(dsp->teakra, 2)) {
        dsp_lle_run_slice(dsp);
    }
    dsp->pipe_addr = Teakra_RecvData(dsp->teakra, 2);
    linfo("loaded dsp component, pipe addr is %04x", dsp->pipe_addr);
    dsp->loaded = true;
}

void dsp_lle_unload_component(DSP* dsp) {
    dsp->loaded = false;
    // finalize communication with dsp
    while (!Teakra_SendDataIsEmpty(dsp->teakra, 2)) dsp_lle_run_slice(dsp);
    Teakra_SendData(dsp->teakra, 2, 0x8000);
    while (!Teakra_RecvDataIsReady(dsp->teakra, 2)) dsp_lle_run_slice(dsp);
    Teakra_RecvData(dsp->teakra, 2);
}