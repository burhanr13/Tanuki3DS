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
    dsp->audio_pipe_pos += len;
}

// the binary pipes are used for aac decoding
void dsp_write_binary_pipe(DSP* dsp, void* buf, u32 len) {}
void dsp_read_binary_pipe(DSP* dsp, void* buf, u32 len) {}

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

void dsp_process_chn(DSPMemory* m, int i, s16* out) {
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

    u32 rem = FRAME_SAMPLES;
    while (true) {
        BufInfo buf;
        if (!get_buf(cfg, stat->cur_buf, &buf)) {
            linfo("ch%d end", i);
            reset_chn(stat);
            break;
        }
        u32 pos = GETDSPU32(stat->pos);
        u32 bufRem = buf.len - pos;
        if (bufRem > rem) bufRem = rem;

        // get bufRem samples from buf

        rem -= bufRem;

        if (rem == 0) {
            SETDSPU32(stat->pos, pos + bufRem);
            break;
        } else SETDSPU32(stat->pos, 0);

        if (!buf.looping) {
            linfo("ch%d to buf%d", i, stat->cur_buf);
            stat->cur_buf++;
            stat->buf_dirty = 1;
        }
    }
}

void dsp_process_frame(DSP* dsp) {
    auto m = get_curr_bank(dsp);

    for (int i = 0; i < DSP_CHANNELS; i++) {
        dsp_process_chn(m, i, nullptr);
    }
}