#ifndef DSP_LLE_H
#define DSP_LLE_H

#include "dsp.h"

#define DSP_SLICE_CYCLES 16384

typedef struct {
    u8 signature[0x100];
    u8 magic[4]; // DSP1
    u32 size;
    u32 memory_layout;
    u8 unk_10c;
    u8 special_segment_type;
    u8 num_segments;
    struct {
        u8 recv_data : 1;
        u8 load_special_segment : 1;
        u8 : 6;
    };
    u32 special_start;
    u32 special_size;
    u64 zero;
    struct {
        u32 offset;
        u32 dspaddr;
        u32 size;
        u8 pad[3];
        u8 type;
        u8 hash[32];
    } segs[10];
} DSP1Header;

void dsp_lle_init(DSP* dsp);
void dsp_lle_destroy(DSP* dsp);

void dsp_lle_run_slice(DSP* dsp);

void dsp_lle_read_pipe(DSP* dsp, u32 index, u8* dst, u32 len);
void dsp_lle_write_pipe(DSP* dsp, u32 index, u8* src, u32 len);

void dsp_lle_load_component(DSP* dsp, void* buf);
void dsp_lle_unload_component(DSP* dsp);

#endif
