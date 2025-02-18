#ifndef DSP_H
#define DSP_H

#include "common.h"
#include "kernel/memory.h"

#define SAMPLE_RATE 32768
#define FRAME_SAMPLES 160

#define DSP_CHANNELS 24

// dsp program is 1ff00000-1ff40000
// dsp data is 1ff40000-1ff80000
#define DSPRAM_DATA_OFF 0x40000
// dsp swaps between the two banks of data
#define DSPRAM_BANK_OFF 0x20000

typedef struct {
#ifdef FASTMEM
    u8* mem;
#else
    E3DSMemory* mem;
#endif

    u32 audio_pipe_pos;

} DSP;

// pipe 2
void dsp_write_audio_pipe(DSP* dsp, void* buf, u32 len);
void dsp_read_audio_pipe(DSP* dsp, void* buf, u32 len);

// pipe 3
void dsp_write_binary_pipe(DSP* dsp, void* buf, u32 len);
void dsp_read_binary_pipe(DSP* dsp, void* buf, u32 len);

void dsp_process_frame(DSP* dsp);

#endif
