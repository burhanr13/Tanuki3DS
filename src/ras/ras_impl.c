#ifdef __aarch64__

#include "ras.h"

#include "common.h"
#include "emulator.h"

void ras_error_cb(rasError err) {
    lerror("ras: %s", rasErrorStrings[err]);
    longjmp(ctremu.exceptionJmp, 1);
}

[[gnu::constructor]] static void init_error() {
    rasSetErrorCallback((rasErrorCallback) ras_error_cb, nullptr);
}

#define RAS_IMPL
#include "ras.h"


#endif
