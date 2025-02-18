#include "dsp.h"

#include "dspstructs.h"

#undef PTR
#ifdef FASTMEM
#define PTR(addr) ((void*) &dsp->mem[addr])
#else
#define PTR(addr) sw_pptr(dsp->mem, addr)
#endif

#define DSPMEM(b)                                                              \
    ((DSPMemory*) PTR(DSPRAM_PBASE + DSPRAM_DATA_OFF + b * DSPRAM_BANK_OFF))

// all the offsets are in 16 bit words
const u16 audio_pipe[16] = {
    15,
#define PUTADDR(name) offsetof(DSPMemory, name) >> 1
    PUTADDR(frame_count),
    PUTADDR(input_cfg),
    PUTADDR(input_status),
    PUTADDR(input_adpcm_coeffs),
    PUTADDR(master_cfg),
    PUTADDR(master_status),
    PUTADDR(output_samples),
    PUTADDR(intermediate_samples),
    PUTADDR(dummy),
    PUTADDR(dummy),
    PUTADDR(dummy),
    PUTADDR(dummy),
    PUTADDR(dummy),
    PUTADDR(dummy),
    PUTADDR(dummy),
#undef PUTADDR
};

void dsp_write_audio_pipe(DSP* dsp, void* buf, u32 len) {
    // writing resets the read position
    dsp->audio_pipe_pos = 0;
}

void dsp_read_audio_pipe(DSP* dsp, void* buf, u32 len) {
    if (dsp->audio_pipe_pos + len > sizeof audio_pipe) {
        lwarn("cannot read from audio pipe");
    }
    memcpy(buf, (void*) audio_pipe + dsp->audio_pipe_pos, len);
}

// the binary pipes are used for aac decoding
void dsp_write_binary_pipe(DSP* dsp, void* buf, u32 len) {}
void dsp_read_binary_pipe(DSP* dsp, void* buf, u32 len) {}

DSPMemory* get_curr_bank(DSP* dsp) {
    auto b0 = DSPMEM(0);
    auto b1 = DSPMEM(1);
    if (b0->frame_count > b1->frame_count) return b0;
    else return b1;
}

void dsp_process_frame(DSP* dsp) {
    auto m [[gnu::unused]] = get_curr_bank(dsp);
    // do stuff ?
}