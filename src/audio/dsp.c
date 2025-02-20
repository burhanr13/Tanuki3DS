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
    u16 id;
    int queuePos; // -1 if not from the queue
} BufInfo;

DSPMemory* get_curr_bank(DSP* dsp) {
    // the bank with higher frame count is the input buffer
    // and lower is output buffer
    // these are swapped every frame
    auto b0 = DSPMEM(0);
    auto b1 = DSPMEM(1);
    if (b0->frame_count > b1->frame_count) {
        memcpy(b1, b0, sizeof *b1);
        return b1;
    } else {
        memcpy(b0, b1, sizeof *b0);
        return b0;
    }
}

void reset_chn(DSPInputStatus* stat) {
    stat->active = 0;
    stat->cur_buf_dirty = 0;
    SETDSPU32(stat->pos, 0);
    stat->cur_buf = 0;
}

bool get_buf(DSPInputConfig* cfg, int bufid, BufInfo* out) {
    if (bufid == 0) return false;
    if (bufid == cfg->buf_id) {
        out->paddr = GETDSPU32(cfg->buf_addr);
        out->len = GETDSPU32(cfg->buf_len);
        out->adpcm = &cfg->buf_adpcm;
        out->looping = cfg->flags.looping;
        out->id = cfg->buf_id;
        out->queuePos = -1;
        return true;
    } else {
        for (int i = 0; i < 4; i++) {
            if (cfg->bufs[i].id != bufid) continue;
            out->paddr = GETDSPU32(cfg->bufs[i].addr);
            out->len = GETDSPU32(cfg->bufs[i].len);
            out->adpcm = &cfg->bufs[i].adpcm;
            out->looping = cfg->bufs[i].looping;
            out->id = cfg->bufs[i].id;
            out->queuePos = i;
            return true;
        }
        return false;
    }
}

void dsp_process_chn(DSP* dsp, DSPMemory* m, int ch, s32* mixer) {
    auto cfg = &m->input_cfg[ch];
    auto stat = &m->input_status[ch];

    // libctru sets this flag when restarting the buffers
    if (cfg->dirty_flags & (BIT(29) | BIT(4))) {
        linfo("ch%d start", ch);
        reset_chn(stat);
        stat->cur_buf = 1;
    }

    cfg->dirty_flags = 0;
    cfg->bufs_dirty = 0;

    stat->active = cfg->active;
    stat->sync_count = cfg->sync_count;
    stat->cur_buf_dirty = 0;

    if (!cfg->active || !stat->cur_buf) return;

    u32 nSamples = FRAME_SAMPLES * cfg->rate;

    s16 lsamples[nSamples] = {};
    s16 rsamples[nSamples] = {};
    u32 curSample = 0;

    u32 rem = nSamples;
    while (rem > 0) {
        BufInfo buf;
        if (!get_buf(cfg, stat->cur_buf, &buf)) {
            linfo("ch%d end", ch);
            reset_chn(stat);
            break;
        }
        u32 bufPos = GETDSPU32(stat->pos);
        u32 bufRem = buf.len - bufPos;
        if (bufRem > rem) bufRem = rem;

        if (cfg->format.num_chan == 2) {
            switch (cfg->format.codec) {
                case DSPFMT_PCM16: {
                    s16* src = PTR(buf.paddr);
                    for (int s = 0; s < bufRem; s++) {
                        lsamples[curSample] = src[2 * bufPos + 0];
                        rsamples[curSample] = src[2 * bufPos + 1];
                        curSample++;
                        bufPos++;
                    }
                    break;
                }
                case DSPFMT_PCM8: {
                    s8* src = PTR(buf.paddr);
                    for (int s = 0; s < bufRem; s++) {
                        lsamples[curSample] = src[2 * bufPos + 0];
                        rsamples[curSample] = src[2 * bufPos + 1];
                        curSample++;
                        bufPos++;
                    }
                    break;
                }
                case DSPFMT_ADPCM:
                    lwarn("stereo adpcm?");
            }
        } else {
            switch (cfg->format.codec) {
                case DSPFMT_PCM16: {
                    // easy
                    s16* src = PTR(buf.paddr);
                    memcpy(&lsamples[curSample], &src[bufPos],
                           bufRem * sizeof(s16));
                    memcpy(&rsamples[curSample], &src[bufPos],
                           bufRem * sizeof(s16));
                    curSample += bufRem;
                    bufPos += bufRem;
                    break;
                }
                case DSPFMT_PCM8: {
                    s8* src = PTR(buf.paddr);
                    for (int s = 0; s < bufRem; s++) {
                        lsamples[curSample] = src[bufPos];
                        rsamples[curSample] = src[bufPos];
                        curSample++;
                        bufPos++;
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

                    s16* coeffs = m->input_adpcm_coeffs[ch];

                    for (int s = 0; s < bufRem; s++) {
                        // every 14 samples there is a new index
                        if (bufPos % 14 == 0) {
                            buf.adpcm->indexScale = *src++;
                        }

                        int diff =
                            (sbi(4))((bufPos++ & 1) ? *src++ : *src >> 4);
                        diff <<= buf.adpcm->scale;
                        // adpcm coeffs are fixed s5.11
                        // samples are fixed s1.15

                        int sample = coeffs[buf.adpcm->index * 2 + 0] *
                                         buf.adpcm->history[0] +
                                     coeffs[buf.adpcm->index * 2 + 1] *
                                         buf.adpcm->history[1];
                        // sample is now fixed s6.26
                        sample += BIT(10); // round instead of floor
                        sample >>= 11;     // make it s6.15
                        sample += diff;
                        // clamp sample back to -1,1
                        if (sample > INT16_MAX) sample = INT16_MAX;
                        if (sample < INT16_MIN) sample = INT16_MIN;

                        // update history
                        buf.adpcm->history[1] = buf.adpcm->history[0];
                        buf.adpcm->history[0] = sample;

                        lsamples[curSample] = sample;
                        rsamples[curSample] = sample;
                        curSample++;
                    }
                    break;
                }
            }
        }

        rem -= bufRem;

        if (bufPos == buf.len) {
            SETDSPU32(stat->pos, 0);
            stat->prev_buf = stat->cur_buf;
            if (!buf.looping) {
                stat->cur_buf++;
                stat->cur_buf_dirty = 1;
                linfo("ch%d to buf%d", ch, stat->cur_buf);
            }
        } else {
            SETDSPU32(stat->pos, bufPos);
        }
    }

    // interpolate samples or something
    // this is the most garbage interpolation ever
    // dsp does 4-channel mixing instead of just 2

    for (int s = 0; s < FRAME_SAMPLES; s++) {
        mixer[2 * s] +=
            (s32) lsamples[s * nSamples / FRAME_SAMPLES] * cfg->mix[0][0];
        mixer[2 * s + 1] +=
            (s32) rsamples[s * nSamples / FRAME_SAMPLES] * cfg->mix[0][1];
    }
}

void dsp_process_frame(DSP* dsp) {
    auto m = get_curr_bank(dsp);

    // interleaved stereo
    s32 mixer[2 * FRAME_SAMPLES] = {};

    for (int i = 0; i < 24; i++) {
        dsp_process_chn(dsp, m, i, mixer);
    }

    // presumably we are supposed to do things here too

    s16 final[2 * FRAME_SAMPLES];
    for (int s = 0; s < 2 * FRAME_SAMPLES; s++) {
        int sample = mixer[s];
        if (sample > INT16_MAX) sample = INT16_MAX;
        if (sample < INT16_MIN) sample = INT16_MIN;
        final[s] = sample;
    }

    if (ctremu.audio_cb) ctremu.audio_cb(final, FRAME_SAMPLES);
}