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
    union {
        u8 indexScale;
        struct {
            u8 scale : 4;
            u8 index : 3;
            u8 : 1;
        };
    };
    u8 _pad;
    s16 history[2];
} ADPCMData;

typedef struct {
    u32 paddr;
    u32 len;
    u32 pos;
    ADPCMData adpcm;
    bool looping;
    bool adpcm_dirty;
    u16 id;
} BufInfo;

typedef struct {
#ifdef FASTMEM
    u8* mem;
#else
    E3DSMemory* mem;
#endif

    u32 audio_pipe_pos;

    // responses on the aac pipe are always 32 bytes
    // and are always read in one go
    u8 binary_pipe[32];
    void* aac_handle;

    // we need to store the whole buffer queue internally
    // because games overwrite them before they finish playing
    FIFO(BufInfo, 4) bufQueues[DSP_CHANNELS];

} DSP;

extern u32 g_dsp_chn_disable;

// pipe 2
void dsp_write_audio_pipe(DSP* dsp, void* buf, u32 len);
void dsp_read_audio_pipe(DSP* dsp, void* buf, u32 len);

// pipe 3
void dsp_write_binary_pipe(DSP* dsp, void* buf, u32 len);
void dsp_read_binary_pipe(DSP* dsp, void* buf, u32 len);

void dsp_process_frame(DSP* dsp);

void dsp_reset(DSP* dsp);

#endif
