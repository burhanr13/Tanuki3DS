#include "dsp.h"

#include "emulator.h"

#include "aac.h"
#include "dspstructs.h"

#include "dspptr.inc"

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
    PUTADDR(unk9),
    PUTADDR(unk10),
    PUTADDR(unk11),
    PUTADDR(unk12),
    PUTADDR(unk13),
    PUTADDR(unk14),
    PUTADDR(unk15),
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
void dsp_write_binary_pipe(DSP* dsp, void* buf, u32 len) {
    if (len != 32) {
        lerror("unknown binary pipe write length %d", len);
        return;
    }
    aac_handle_request(dsp, buf, (void*) dsp->binary_pipe);
}

void dsp_read_binary_pipe(DSP* dsp, void* buf, u32 len) {
    if (len != 32) {
        lerror("unknown binary pipe read length %d", len);
        return;
    }
    memcpy(buf, dsp->binary_pipe, sizeof dsp->binary_pipe);
}

DSPMemory* get_curr_bank(DSP* dsp) {
    // the bank with higher frame count is the input buffer
    // and lower is output buffer
    // these are swapped every frame
    auto b0 = DSPMEM(0);
    auto b1 = DSPMEM(1);
    u32 fc0 = b0->frame_count;
    u32 fc1 = b1->frame_count;
    // handle frame count overflow
    if (fc1 == 0xffff && fc0 != 0xfffe) fc0 += 0x10000;
    if (fc0 == 0xffff && fc1 != 0xfffe) fc1 += 0x10000;
    if (fc0 > fc1) {
        memcpy(b1, b0, sizeof *b1);
        return b1;
    } else {
        memcpy(b0, b1, sizeof *b0);
        return b0;
    }
}

void reset_chn(DSP* dsp, int ch, DSPInputStatus* stat) {
    stat->active = 0;
    SETDSPU32(stat->pos, 0);
    stat->cur_buf = 0;
    FIFO_clear(dsp->bufQueues[ch]);
}

void get_buf(DSPInputConfig* cfg, int bufid, BufInfo* out) {
    if (bufid == 0) out->id = 0;
    if (bufid == cfg->buf_id) {
        out->paddr = GETDSPU32(cfg->buf_addr);
        out->len = GETDSPU32(cfg->buf_len);
        // embedded buffer start position is specified
        out->pos = GETDSPU32(cfg->play_pos);
        out->adpcm = cfg->buf_adpcm;
        out->looping = cfg->flags.looping;
        out->adpcm_dirty = cfg->flags.adpcm_dirty;
        out->id = cfg->buf_id;
        out->queuePos = -1;
    } else {
        for (int i = 0; i < 4; i++) {
            if (cfg->bufs[i].id != bufid) continue;
            out->paddr = GETDSPU32(cfg->bufs[i].addr);
            out->len = GETDSPU32(cfg->bufs[i].len);
            out->pos = 0;
            out->adpcm = cfg->bufs[i].adpcm;
            out->looping = cfg->bufs[i].looping;
            out->adpcm_dirty = cfg->bufs[i].adpcm_dirty;
            out->id = cfg->bufs[i].id;
            out->queuePos = i;
            return;
        }
        out->id = 0;
    }
}

// update any internally stored buffers that were modified
// externally
void update_bufs(DSP* dsp, int ch, DSPInputConfig* cfg) {
    FIFO_foreach(i, dsp->bufQueues[ch]) {
        auto old = &dsp->bufQueues[ch].d[i];
        BufInfo new;
        get_buf(cfg, old->id, &new);
        if (new.id != old->id ||
            (new.queuePos >= 0 && !(cfg->bufs_dirty & BIT(new.queuePos))))
            continue;
        old->paddr = new.paddr;
        old->len = new.len;
        old->looping = new.looping;
    }
}

// queue new buffers that have been added
void refill_bufs(DSP* dsp, int ch, DSPInputConfig* cfg) {
    int curBufid =
        dsp->bufQueues[ch].size ? FIFO_back(dsp->bufQueues[ch]).id + 1 : 1;
    while (dsp->bufQueues[ch].size < FIFO_MAX(dsp->bufQueues[ch])) {
        BufInfo b;
        get_buf(cfg, curBufid++, &b);
        if (b.id == 0) break;
        FIFO_push(dsp->bufQueues[ch], b);
    }
}

void dsp_process_chn(DSP* dsp, DSPMemory* m, int ch, s32 (*mixer)[2]) {
    auto cfg = &m->input_cfg[ch];
    auto stat = &m->input_status[ch];

    stat->sync_count = cfg->sync_count;

    u16 og_cur_buf = stat->cur_buf;

    // these are supposedly reset and "partial reset" flags
    // i dont know what the difference is
    if (cfg->dirty_flags & (BIT(29) | BIT(4))) {
        linfo("ch%d reset", ch);
        reset_chn(dsp, ch, stat);
    }

    stat->active = cfg->active;

    // this bit is the embedded buffer dirty bit
    // which begins playback of a new embedded buffer
    // if it is not set and there is nothing in the queue
    // then the channel is done playing
    if (!dsp->bufQueues[ch].size && !(cfg->dirty_flags & BIT(30))) {
        reset_chn(dsp, ch, stat);
    }

    cfg->dirty_flags = 0;

    if (!stat->active) {
        stat->cur_buf_dirty = stat->cur_buf != og_cur_buf;
        return;
    }

    update_bufs(dsp, ch, cfg);
    refill_bufs(dsp, ch, cfg);

    cfg->bufs_dirty = 0;

    u32 nSamples = FRAME_SAMPLES * cfg->rate;

    s16 lsamples[nSamples] = {};
    s16 rsamples[nSamples] = {};
    u32 curSample = 0;

    u32 rem = nSamples;
    while (rem > 0) {
        // no more data right now
        if (!dsp->bufQueues[ch].size) break;

        BufInfo* buf = &FIFO_peek(dsp->bufQueues[ch]);

        u32 bufRem = buf->len - buf->pos;
        if (bufRem > rem) bufRem = rem;

        linfo("ch%d playing buf %d at pos %d for %d samples", ch, buf->id,
              buf->pos, bufRem);

        if (cfg->format.num_chan == 2) {
            switch (cfg->format.codec) {
                case DSPFMT_PCM16: {
                    s16(*src)[2] = PTR(buf->paddr);
                    for (int s = 0; s < bufRem; s++) {
                        lsamples[curSample] = src[buf->pos][0];
                        rsamples[curSample] = src[buf->pos][1];
                        curSample++;
                        buf->pos++;
                    }
                    break;
                }
                case DSPFMT_PCM8: {
                    s8(*src)[2] = PTR(buf->paddr);
                    for (int s = 0; s < bufRem; s++) {
                        lsamples[curSample] = src[buf->pos][0];
                        rsamples[curSample] = src[buf->pos][1];
                        curSample++;
                        buf->pos++;
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
                    s16* src = PTR(buf->paddr);
                    memcpy(&lsamples[curSample], &src[buf->pos],
                           bufRem * sizeof(s16));
                    memcpy(&rsamples[curSample], &src[buf->pos],
                           bufRem * sizeof(s16));
                    curSample += bufRem;
                    buf->pos += bufRem;
                    break;
                }
                case DSPFMT_PCM8: {
                    s8* src = PTR(buf->paddr);
                    for (int s = 0; s < bufRem; s++) {
                        lsamples[curSample] = src[buf->pos];
                        rsamples[curSample] = src[buf->pos];
                        curSample++;
                        buf->pos++;
                    }
                    break;
                }
                case DSPFMT_ADPCM: {
                    // https://github.com/Thealexbarney/DspTool/blob/master/dsptool/decode.c

                    u8* src = PTR(buf->paddr);
                    // get back to where we were in the buffer
                    // i am assuming play pos does not count the index bytes
                    // as samples
                    src += (buf->pos / 14) * 8;
                    if (buf->pos % 14 > 0) {
                        src += 1 + (buf->pos % 14) / 2;
                    }

                    s16* coeffs = m->input_adpcm_coeffs[ch];

                    for (int s = 0; s < bufRem; s++) {
                        // every 14 samples there is a new index
                        if (buf->pos % 14 == 0) {
                            buf->adpcm.indexScale = *src++;
                        }

                        int diff =
                            (sbi(4))((buf->pos++ & 1) ? *src++ : *src >> 4);
                        diff <<= buf->adpcm.scale;
                        // adpcm coeffs are fixed s5.11
                        // samples are fixed s1.15

                        int sample = coeffs[buf->adpcm.index * 2 + 0] *
                                         buf->adpcm.history[0] +
                                     coeffs[buf->adpcm.index * 2 + 1] *
                                         buf->adpcm.history[1];
                        // sample is now fixed s6.26
                        sample += BIT(10); // round instead of floor
                        sample >>= 11;     // make it s6.15
                        sample += diff;
                        // clamp sample back to -1,1
                        if (sample > INT16_MAX) sample = INT16_MAX;
                        if (sample < INT16_MIN) sample = INT16_MIN;

                        // update history
                        buf->adpcm.history[1] = buf->adpcm.history[0];
                        buf->adpcm.history[0] = sample;

                        lsamples[curSample] = sample;
                        rsamples[curSample] = sample;
                        curSample++;
                    }
                    break;
                }
            }
        }

        rem -= bufRem;

        if (buf->pos == buf->len) {
            stat->prev_buf = buf->id;
            if (buf->looping) {
                buf->pos = 0;
            } else {
                BufInfo b;
                FIFO_pop(dsp->bufQueues[ch], b);
                if (dsp->bufQueues[ch].size)
                    FIFO_peek(dsp->bufQueues[ch]).adpcm = b.adpcm;
            }
        }
    }

    if (dsp->bufQueues[ch].size) {
        stat->cur_buf = FIFO_peek(dsp->bufQueues[ch]).id;
        SETDSPU32(stat->pos, FIFO_peek(dsp->bufQueues[ch]).pos);
    } else {
        linfo("ch%d ending at %d", ch, stat->prev_buf);
        reset_chn(dsp, ch, stat);
    }

    stat->cur_buf_dirty = stat->cur_buf != og_cur_buf;

    // interpolate samples or something
    // this is the most garbage interpolation ever
    // dsp does 4-channel mixing instead of just 2

    for (int s = 0; s < FRAME_SAMPLES; s++) {
        mixer[s][0] +=
            (s32) lsamples[s * nSamples / FRAME_SAMPLES] * cfg->mix[0][0];
        mixer[s][1] +=
            (s32) rsamples[s * nSamples / FRAME_SAMPLES] * cfg->mix[0][1];
    }
}

s16 clamp16(s32 in) {
    if (in > INT16_MAX) return INT16_MAX;
    if (in < INT16_MIN) return INT16_MIN;
    return in;
}

void dsp_process_frame(DSP* dsp) {

    auto m = get_curr_bank(dsp);

    // interleaved stereo
    s32 mixer[FRAME_SAMPLES][2] = {};

    for (int i = 0; i < 24; i++) {
        dsp_process_chn(dsp, m, i, mixer);
    }

    // presumably we are supposed to do things here too

    s16 final[FRAME_SAMPLES][2];
    for (int s = 0; s < FRAME_SAMPLES; s++) {
        final[s][0] = clamp16(mixer[s][0] * m->master_cfg.master_vol);
        final[s][1] = clamp16(mixer[s][1] * m->master_cfg.master_vol);
    }

    if (ctremu.audio_cb) ctremu.audio_cb(final, FRAME_SAMPLES);
}

void dsp_reset(DSP* dsp) {
    dsp->audio_pipe_pos = 0;
    for (int i = 0; i < DSP_CHANNELS; i++) {
        FIFO_clear(dsp->bufQueues[i]);
    }
}