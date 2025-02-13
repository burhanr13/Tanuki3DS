#include "ipc.h"

KSession* session_create(PortRequestHandler f) {
    KSession* session = calloc(1, sizeof *session);
    session->hdr.type = KOT_SESSION;
    session->handler = (PortRequestHandlerArg) f;
    return session;
}

KSession* session_create_arg(PortRequestHandlerArg f, u64 arg) {
    KSession* session = calloc(1, sizeof *session);
    session->hdr.type = KOT_SESSION;
    session->handler = f;
    session->arg = arg;
    return session;
}