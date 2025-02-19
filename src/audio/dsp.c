#include "dsp.h"

#include "emulator.h"

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
    dsp->audio_pipe_pos += len;
}

// the binary pipes are used for aac decoding
void dsp_write_binary_pipe(DSP* dsp, void* buf, u32 len) {}
void dsp_read_binary_pipe(DSP* dsp, void* buf, u32 len) {
    memset(buf, 0, len);
}

typedef struct {
    u32 paddr;
    u32 len;
    ADPCMData* adpcm;
    bool looping;
} BufInfo;

DSPMemory* get_curr_bank(DSP* dsp) {
    auto b0 = DSPMEM(0);
    auto b1 = DSPMEM(1);
    if (b0->frame_count > b1->frame_count) return b0;
    else return b1;
}

void reset_chn(DSPInputStatus* stat) {
    stat->active = 0;
    stat->buf_dirty = 0;
    SETDSPU32(stat->pos, 0);
    stat->cur_buf = 0;
    stat->prev_buf = 0;
}

bool get_buf(DSPInputConfig* cfg, int bufid, BufInfo* out) {
    if (bufid == 0) return false;
    if (bufid == 1) {
        if (cfg->buf_id == 0) return false;
        if (cfg->buf_id != 1) {
            lwarn("?");
            return false;
        }
        out->paddr = GETDSPU32(cfg->buf_addr);
        out->len = GETDSPU32(cfg->buf_len);
        out->adpcm = &cfg->buf_adpcm;
        out->looping = cfg->flags.looping;
        return true;
    } else {
        for (int i = 0; i < 4; i++) {
            if (cfg->bufs[i].id != bufid) continue;
            out->paddr = GETDSPU32(cfg->bufs[i].addr);
            out->len = GETDSPU32(cfg->bufs[i].len);
            out->adpcm = &cfg->bufs[i].adpcm;
            out->looping = cfg->bufs[i].looping;
            return true;
        }
        return false;
    }
}

void dsp_process_chn(DSP* dsp, DSPMemory* m, int i, s32* mixer) {
    auto cfg = &m->input_cfg[i];
    auto stat = &m->input_status[i];

    // libctru sets this flag when restarting the buffers
    if ((cfg->dirty_flags & 0x40200010) == 0x40200010) {
        linfo("ch%d start", i);
        reset_chn(stat);
        stat->cur_buf = 1;
    }

    cfg->dirty_flags = 0;

    stat->active = cfg->active;
    stat->sync_count = cfg->sync_count;

    if (!cfg->active || !stat->cur_buf) return;

    // todo: handle rate, gain, stereo, and everything else
    s16 samples[FRAME_SAMPLES] = {};
    u32 curSample = 0;

    u32 rem = FRAME_SAMPLES;
    while (true) {
        BufInfo buf;
        if (!get_buf(cfg, stat->cur_buf, &buf)) {
            linfo("ch%d end", i);
            reset_chn(stat);
            break;
        }
        u32 bufPos = GETDSPU32(stat->pos);
        u32 bufRem = buf.len - bufPos;
        if (bufRem > rem) bufRem = rem;

        if (cfg->format.num_chan == 2) {
            switch (cfg->format.codec) {
                case DSPFMT_PCM16:
                    s16* src = PTR(buf.paddr);
                    for (int s = 0; s < bufRem; s++) {
                        samples[curSample + s] = src[2 * (bufPos + s)];
                    }
                    break;
            }
        } else {
            switch (cfg->format.codec) {
                case DSPFMT_PCM16: {
                    // easy
                    s16* src = PTR(buf.paddr);
                    memcpy(&samples[curSample], &src[bufPos],
                           bufRem * sizeof(s16));
                    break;
                }
                case DSPFMT_PCM8: {
                    s8* src = PTR(buf.paddr);
                    for (int s = 0; s < bufRem; s++) {
                        samples[curSample + s] = src[bufPos + s];
                    }
                    break;
                }
                case DSPFMT_ADPCM: {
                    // https://github.com/Thealexbarney/DspTool/blob/master/dsptool/decode.c

                    u8* src = PTR(buf.paddr);
                    // get back to where we were in the buffer
                    // i am assuming play pos does not count the index bytes
                    // as samples
                    src += (bufPos / 14) * 8;
                    if (bufPos % 14 > 0) {
                        src += 1 + (bufPos % 14) / 2;
                    }

                    s16* coeffs = m->input_adpcm_coeffs[i];

                    for (int s = 0; s < bufRem; s++) {
                        int srcIdx = bufPos + s;
                        // every 14 samples there is a new index
                        if (srcIdx % 14 == 0) {
                            buf.adpcm->indexScale = *src++;
                        }

                        int diff = (sbi(4))((bufPos & 1) ? *src++ : *src >> 4);
                        diff <<= buf.adpcm->scale;
                        // adpcm coeffs are fixed 1.4.11
                        // samples are fixed 1.15

                        int sample = coeffs[buf.adpcm->index * 2 + 0] *
                                         buf.adpcm->history[0] +
                                     coeffs[buf.adpcm->index * 2 + 1] *
                                         buf.adpcm->history[1];
                        // sample is now fixed 1.4.26
                        sample >>= 11; // make it 1.4.15
                        sample += diff;
                        // clamp sample back to -1,1
                        if (sample > INT16_MAX) sample = INT16_MAX;
                        if (sample < INT16_MIN) sample = INT16_MIN;

                        // update history
                        buf.adpcm->history[1] = buf.adpcm->history[0];
                        buf.adpcm->history[0] = sample;

                        samples[curSample + s] = sample;
                    }
                    break;
                }
            }
        }

        curSample += bufRem;
        rem -= bufRem;

        if (rem == 0) {
            SETDSPU32(stat->pos, bufPos + bufRem);
            break;
        } else SETDSPU32(stat->pos, 0);

        if (!buf.looping) {
            linfo("ch%d to buf%d", i, stat->cur_buf);
            stat->prev_buf = stat->cur_buf++;
            stat->buf_dirty = 1;
        }
    }

    // interpolate samples or something

    for (int i = 0; i < FRAME_SAMPLES; i++) {
        mixer[i] += samples[i];
    }
}

void dsp_process_frame(DSP* dsp) {
    auto m = get_curr_bank(dsp);

    // please excuse my sorry excuse of a mixer
    s32 mixer[FRAME_SAMPLES] = {};

    for (int i = 0; i < 24; i++) {
        dsp_process_chn(dsp, m, i, mixer);
    }

    // presumably we are supposed to do things here too

    s16 final[FRAME_SAMPLES];
    for (int s = 0; s < FRAME_SAMPLES; s++) {
        int sample = mixer[s];
        if (sample > INT16_MAX) sample = INT16_MAX;
        if (sample < INT16_MIN) sample = INT16_MIN;
        final[s] = sample;
    }

    if (ctremu.audio_cb) ctremu.audio_cb(final, FRAME_SAMPLES);
}