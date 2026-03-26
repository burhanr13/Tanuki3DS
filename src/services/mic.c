#include "mic.h"

#include "3ds.h"

DECL_PORT(mic) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001:
            linfo("MapSharedMem with handle %08x", cmdbuf[3]);
            s->services.mic.shmem = HANDLE_GET_TYPED(cmdbuf[3], KOT_SHAREDMEM);
            if (s->services.mic.shmem) s->services.mic.shmem->hdr.refcount++;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0003: {
            if (!s->services.mic.shmem) {
                lerror("sharedmem not mapped");
                cmdbuf[0] = IPCHDR(1, 0);
                cmdbuf[1] = -1;
                break;
            }
            u8 enc = cmdbuf[1];
            static const int rates[] = {32730, 16360, 10910, 8180};
            int rate = rates[cmdbuf[2] & 3];
            u32 off = cmdbuf[3];
            u32 sz = cmdbuf[4];
            bool loop = cmdbuf[5];
            if (off != 0 || sz != s->services.mic.shmem->size - 4) {
                lwarn("unimpl sampling off/sz %d %d", off, sz);
            }
            linfo("StartSampling enc=%d rate=%d loop=%d", enc, rate, loop);
            s->services.mic.sampling = true;
            s->services.mic.encoding = enc;
            s->services.mic.sampleRate = rate;
            s->services.mic.loop = loop;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0005:
            linfo("StopSampling");
            s->services.mic.sampling = false;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0007: {
            u32 handle = srvobj_make_handle(s, &s->services.mic.event.hdr);
            linfo("GetEventHandle %08x", handle);
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = handle;
            break;
        }
        case 0x0008:
            s->services.mic.gain = cmdbuf[1];
            linfo("SetGain %d", s->services.mic.gain);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x000a:
            linfo("SetPower");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}

void mic_send_data(E3DS* s, void* src, u32 srclen) {
    auto mic = &s->services.mic;
    if (!mic->sampling) return;
    void* dst = PTR(mic->shmem->mapaddr);
    u32 dstlen = mic->shmem->size - 4;
    u32* curoff = (u32*) (dst + dstlen);

    while (srclen > 0) {
        u32 rem = dstlen - *curoff;
        if (rem > srclen) rem = srclen;
        memcpy(dst + *curoff, src, rem);
        if ((*curoff += rem) == dstlen) {
            *curoff = 0;
            if (!mic->loop) {
                mic->sampling = false;
                break;
            }
        }
        srclen -= rem;
        src += rem;
    }
    linfo("signaling mic event");
    event_signal(s, &mic->event);
}
