#include "aac.h"

#include <fdk-aac/aacdecoder_lib.h>

#include "dspptr.inc"

void aac_init(DSP* dsp) {
    // these are the parameters panda3ds uses, idk what they mean
    dsp->aac_handle = aacDecoder_Open(TT_MP4_ADTS, 1);
    aacDecoder_SetParam(dsp->aac_handle, AAC_PCM_MAX_OUTPUT_CHANNELS, 2);
}

void aac_shutdown(DSP* dsp) {
    if (dsp->aac_handle) aacDecoder_Close(dsp->aac_handle);
    dsp->aac_handle = nullptr;
}

int rate_to_enum(int rate) {
    int rates[9] = {48000, 44100, 32000, 24000, 22050,
                    16000, 12000, 11025, 8000};
    for (int i = 0; i < 9; i++) {
        if (rate == rates[i]) return i;
    }
    lerror("unknown sample rate");
    return 0;
}

void aac_handle_request(DSP* dsp, DSPAACMessage* in, DSPAACMessage* out) {
    out->mode = in->mode;
    out->command = in->command;
    out->result = 0;

    if (in->command != AACCMD_CONVERT) {
        switch (in->command) {
            case AACCMD_INIT:
                aac_init(dsp);
                break;
            case AACCMD_SHUTDOWN:
                aac_shutdown(dsp);
                break;
        }
        return;
    }
    if (in->mode != AACMODE_DECODE) {
        lerror("unknown aac mode");
        out->result = -1;
        return;
    }
    if (!dsp->aac_handle) {
        lerror("aac was not initialized");
        out->result = -1;
        return;
    }

    u8* src = PTR(in->decodeRequest.paddrIn);
    s16* dstL = PTR(in->decodeRequest.paddrOutL);
    s16* dstR = nullptr;
    if (in->decodeRequest.paddrOutR) dstR = PTR(in->decodeRequest.paddrOutR);

    out->decodeResponse.size = in->decodeRequest.sizeIn;

    u32 size = in->decodeRequest.sizeIn;
    u32 bytesValid = size;

    ldebug("aac decoding %d bytes", size);

    out->decodeResponse.nSamples = 0;

    int rate = 0;

    AAC_DECODER_ERROR res;

    while (bytesValid) {
        res = aacDecoder_Fill(dsp->aac_handle, &src, &size, &bytesValid);
        if (res != AAC_DEC_OK) lerror("aac failed: %d", res);

        // largest size of an aac stereo frame
        s16 buf[2048];
        res = aacDecoder_DecodeFrame(dsp->aac_handle, buf, 2048, 0);
        if (res != AAC_DEC_OK) lerror("aac failed: %d", res);

        auto info = aacDecoder_GetStreamInfo(dsp->aac_handle);

        out->decodeResponse.nChannels = info->numChannels;
        out->decodeResponse.nSamples += info->frameSize;
        rate = info->sampleRate;

        memcpy(dstL, buf, info->frameSize * sizeof(s16));
        dstL += info->frameSize;
        if (info->numChannels == 2) {
            memcpy(dstR, buf + info->frameSize, info->frameSize * sizeof(s16));
            dstR += info->frameSize;
        }
    }

    out->decodeResponse.rate = rate_to_enum(rate);

    ldebug("decoded %d samples, %d channels at rate %d",
           out->decodeResponse.nSamples, out->decodeResponse.nChannels, rate);
}