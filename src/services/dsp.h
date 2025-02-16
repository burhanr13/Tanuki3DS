#ifndef DSP_SRV_H
#define DSP_SRV_H

#include <teakra/teakra_c.h>

#include "kernel/thread.h"

#include "srv.h"

#define DSP_SLICE_CYCLES 16384

#define DSPRAM_DATA_OFF 0x40000

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

typedef struct {
    // there are 3 interrupts and 4 channels to register
    // events for
    KEvent* events[3][4];

    KEvent sem_event; // signalled when there is new audio data
    u16 sem_mask;

    bool component_loaded;
    u16 pipe_addr; // address of the pipes
    // the pipe event is signaled when both have happened
    bool sem_signaled;
    bool data_signaled;

    TeakraContext* teakra;
} DSPData;

DECL_PORT(dsp);

void dsp_lle_init(E3DS* s);
void dsp_lle_destroy(E3DS* s);

void dsp_lle_run_slice(E3DS* s);
void dsp_lle_run_event(E3DS* s, u32);

void dsp_lle_read_pipe(E3DS* s, u32 index, u8* dst, u32 len);
void dsp_lle_write_pipe(E3DS* s, u32 index, u8* src, u32 len);

void dsp_lle_load_component(E3DS* s, void* buf);

#endif
