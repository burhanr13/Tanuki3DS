#include "dsp.h"

#include "3ds.h"
#include "audio/dsp.h"

void dsp_event(E3DS* s, SchedEventArg) {
    s->lastAudioFrame = s->sched.now;
    if (s->services.dsp.audio_event)
        event_signal(s, s->services.dsp.audio_event);
}

// runs when the application signals the dsp semaphore
// that a new audio frame is ready
void sem_event_handler(E3DS* s) {
    linfo("dsp sem signaled mask=%04x", s->services.dsp.sem_mask);
    if (!s->services.dsp.comp_loaded) return;
    dsp_process_frame(&s->dsp);

    // make sure audio frames are being sent at a consistent rate
    s64 timeSinceLastFrame = s->sched.now - s->lastAudioFrame;
    u64 audioFrameCycles = CPU_CLK * FRAME_SAMPLES / SAMPLE_RATE;
    if (timeSinceLastFrame >= audioFrameCycles) timeSinceLastFrame = 0;

    add_event(&s->sched, dsp_event, SEA_NONE,
              audioFrameCycles - timeSinceLastFrame);
}

DECL_PORT(dsp) {
    u32* cmdbuf = PTR(cmd_addr);

    switch (cmd.command) {
        case 0x0001: {
            int reg = cmdbuf[1];
            linfo("RecvData %d", reg);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            // this is only used directly by the application
            // to check whether the dsp is on
            if (reg == 0) {
                cmdbuf[2] = 1;
            } else {
                lwarn("unknown channel for recv data");
            }
            break;
        }
        case 0x0002: {
            int reg = cmdbuf[1];
            linfo("RecvDataIsReady %d", reg);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 1; // hle dsp is always ready
            break;
        }
        case 0x0007:
            linfo("SetSemaphore %x", cmdbuf[1]);
            // not useful in hle
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x000c: {
            linfo("ConvertProcessAddressFromDspDram");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[2] = DSPRAM_VBASE + DSPRAM_DATA_OFF + (cmdbuf[1] << 1);
            cmdbuf[1] = 0;
            break;
        }
        case 0x000d: {
            u32 chan = cmdbuf[1];
            u32 size = cmdbuf[2];
            void* buf = PTR(cmdbuf[4]);
            linfo("WriteProcessPipe ch=%d, sz=%d", chan, size);
            switch (chan) {
                case 2:
                    dsp_write_audio_pipe(&s->dsp, buf, size);
                    if (s->services.dsp.audio_event)
                        event_signal(s, s->services.dsp.audio_event);
                    break;
                case 3:
                    dsp_write_binary_pipe(&s->dsp, buf, size);
                    if (s->services.dsp.binary_event)
                        event_signal(s, s->services.dsp.binary_event);
                    break;
                default:
                    lwarn("unknown audio pipe");
            }
            break;
        }
        case 0x0010: {
            u32 chan = cmdbuf[1];
            u32 size = (u16) cmdbuf[3];
            void* buf = PTR(cmdbuf[0x41]);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = size;

            linfo("ReadPipeIfPossible chan=%d with size 0x%x", chan, size);
            switch (chan) {
                case 2:
                    dsp_read_audio_pipe(&s->dsp, buf, size);
                    break;
                case 3:
                    dsp_read_binary_pipe(&s->dsp, buf, size);
                    break;
                default:
                    lwarn("unknown audio pipe");
            }
            break;
        }
        case 0x0011: {
            linfo("LoadComponent");
            u32 size [[gnu::unused]] = cmdbuf[1];
            void* buf [[gnu::unused]] = PTR(cmdbuf[5]);

            // component is not directly used right now
            s->services.dsp.comp_loaded = true;

            cmdbuf[0] = IPCHDR(2, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = true;
            cmdbuf[3] = cmdbuf[4];
            cmdbuf[4] = cmdbuf[5];
            break;
        }
        case 0x0012:
            linfo("UnloadComponent");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            dsp_reset(&s->dsp);
            remove_event(&s->sched, dsp_event, SEA_NONE);
            s->services.dsp.comp_loaded = false;
            break;
        case 0x0013:
            linfo("FlushDataCache");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0014:
            linfo("InvalidateDCache");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0015: {
            int interrupt = cmdbuf[1];
            int channel = cmdbuf[2];
            linfo("RegisterInterruptEvents int=%d,ch=%d with handle %x",
                  interrupt, channel, cmdbuf[4]);

            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;

            KEvent** event;
            if (interrupt != 2) {
                lwarn("unknown interrupt event");
                break;
            } else {
                if (channel == 2) event = &s->services.dsp.audio_event;
                else if (channel == 3) event = &s->services.dsp.binary_event;
                else {
                    lwarn("unknown channel for pipe event");
                    break;
                }
            }

            // unregister an existing event
            if (*event) {
                (*event)->hdr.refcount--;
                *event = nullptr;
            }

            *event = HANDLE_GET_TYPED(cmdbuf[4], KOT_EVENT);
            if (*event) {
                (*event)->hdr.refcount++;
            }
            break;
        }
        case 0x0016:
            linfo("GetSemaphoreEventHandle");
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            s->services.dsp.sem_event.callback = sem_event_handler;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.dsp.sem_event.hdr);
            break;
        case 0x0017:
            linfo("SetSemaphoreMask %x", cmdbuf[1]);
            s->services.dsp.sem_mask = cmdbuf[1];
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x001f:
            linfo("GetHeadphoneStatus");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = true;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}
