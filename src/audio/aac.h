#ifndef AAC_H
#define AAC_H

#include "common.h"
#include "dsp.h"

enum : u16 {
    AACMODE_NONE,
    AACMODE_DECODE,
    AACMODE_ENCODE,
};
enum {
    AACCMD_INIT,
    AACCMD_CONVERT,
    AACCMD_SHUTDOWN,
    AACCMD_LOAD,
    AACCMD_SAVE,
};

typedef struct {
    u16 mode;
    u16 command;
    u32 result;
    union {
        struct {
            u32 paddrIn;
            u32 sizeIn;
            u32 paddrOutL;
            u32 paddrOutR;
            u32 unk[2];
        } decodeRequest;
        struct {
            u32 rate;
            u32 nChannels;
            u32 size;
            u32 unk[2];
            u32 nSamples;
        } decodeResponse;
    };
} DSPAACMessage;

void aac_init(DSP* dsp);
void aac_shutdown(DSP* dsp);
void aac_handle_request(DSP* dsp, DSPAACMessage* in, DSPAACMessage* out);

#endif
