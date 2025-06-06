#ifndef IPC_H
#define IPC_H

#include "common.h"
#include "kernel/kernel.h"

typedef struct _3DS E3DS;

typedef union {
    u32 w;
    struct {
        u32 paramsize_translate : 6;
        u32 paramsize_normal : 6;
        u32 : 4;
        u32 command : 16;
    };
} IPCHeader;

#define IPCHDR(normal, translate)                                              \
    ((IPCHeader) {.paramsize_normal = normal,                                  \
                  .paramsize_translate = translate}                            \
         .w)

typedef void (*PortRequestHandler)(E3DS* s, IPCHeader cmd, u32 cmd_addr,
                                   u64* delay);
typedef void (*PortRequestHandlerArg)(E3DS* s, IPCHeader cmd, u32 cmd_addr,
                                      u64* delay, u64 arg);

typedef struct {
    KObject hdr;

    PortRequestHandlerArg handler;
    u64 arg;
} KSession;

KSession* session_create(PortRequestHandler f);
KSession* session_create_arg(PortRequestHandlerArg f, u64 arg);

#define DECL_PORT(name)                                                        \
    void port_handle_##name(E3DS* s, IPCHeader cmd, u32 cmd_addr, u64* delay)

#define DECL_PORT_ARG(name, arg)                                               \
    void port_handle_##name(E3DS* s, IPCHeader cmd, u32 cmd_addr, u64* delay,  \
                            u64 arg)

#endif
