#ifndef DSPSTRUCTS_H
#define DSPSTRUCTS_H

#include "common.h"
#include "dsp.h"

typedef struct {
    u8 scale;
    u8 _pad;
    s16 y[2];
} ADPCMData;

typedef struct {
    u32 addr;
    u32 len;
    ADPCMData adpcm;
    u8 adpcm_dirty;
    u8 looping;
    u16 id;
    u16 _pad;
} DSPBuffer;

// there are 15 dsp structs whose addresses are returned by the pipe

// 1
typedef u16 DSPFrameCount;

// 2
typedef struct {
    u32 dirty;
    float gain[3][2][2];
    float rate;
    u8 interp_mode;
    u8 polyphase;
    u16 filter;
    s16 simple_filter[2];
    s16 biquad_filter[5];
    u16 buf_dirty;
    DSPBuffer bufs[4];
    u32 _156;
    u16 active;
    u16 sync_count;
    u32 play_pos;
    u32 _168;
    u32 embed_buf_addr;
    u32 embed_buf_len;
    struct {
        u16 num_chan : 2;
        u16 codec : 2;
        u16 : 1;
        u16 fade : 1;
        u16 : 10;
    } format;
    ADPCMData embed_buf_adpcm;
    u16 flags;
    u16 embed_buf_id;
} DSPInputConfig;

// 3
typedef struct {
    u8 enabled;
    u8 dirty;
    u16 sync_count;
    u32 pos;
    u16 cur_buf;
    u16 prev_buf;
} DSPInputStatus;

// 4
typedef s16 DSPInputADPCMCoeffs[16];

// 5, this struct is only in libctru for some reason
// i have no idea what any of these mean
typedef struct {
    float master_vol;
    float aux_vol[2];
    u16 out_buf_count;
    u16 _14[2];
    u16 output_mode;
    u16 clip_mode;
    u16 headset;
    u16 surround_depth;
    u16 surround_pos;
    u16 _28;
    u16 rear_ratio;
    u16 aux_front_bypass[2];
    u16 aux_bus_enable[2];
    u16 delay[2][10];
    u16 reverb[2][26];
    u16 sync_mode;
    u16 _186;
    u32 _188;
} DSPConfig;

// 6
typedef u16 DSPStatus[16];

// 7
typedef s16 DSPOutputSamples[2][FRAME_SAMPLES];

// 8
typedef s32 DSPIntermediateSamples[FRAME_SAMPLES][4];

// 9-16 are undocumented

typedef struct {
    DSPFrameCount frame_count;
    DSPInputConfig input_cfg[DSP_CHANNELS];
    DSPInputStatus input_status[DSP_CHANNELS];
    DSPInputADPCMCoeffs input_adpcm_coeffs[DSP_CHANNELS];
    DSPConfig master_cfg;
    DSPStatus master_status;
    DSPOutputSamples output_samples;
    DSPIntermediateSamples intermediate_samples;
    u32 dummy; // addresses 9-15 will point here
} DSPMemory;

#endif