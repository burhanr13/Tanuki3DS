#include "applets.h"

#include "emulator.h"
#include "unicode.h"

void swkbd_run(E3DS* s, u32 paramsvaddr, u32 shmemvaddr) {

    linfo("running swkbd");

    s->services.apt.swkbd.paramsvaddr = paramsvaddr;
    s->services.apt.swkbd.shmemvaddr = shmemvaddr;
    ctremu.needs_swkbd = true;
}

void swkbd_resp(E3DS* s, char* text) {
    linfo("got response text: %s", text);
    ctremu.needs_swkbd = false;

    SwkbdState* in = PTR(s->services.apt.swkbd.paramsvaddr);
    SwkbdState* out = (SwkbdState*) &s->services.apt.nextparam.param;

    memset(out, 0, sizeof *out);

    // set result to the confirm button
    int result = 0;
    switch (in->num_buttons_m1 + 1) {
        case 1:
            result = SWKBD_D0_CLICK;
            break;
        case 2:
            result = SWKBD_D1_CLICK1;
            break;
        case 3:
            result = SWKBD_D2_CLICK2;
            break;
    }

    out->result = result;

    u16* outtxt = PTR(s->services.apt.swkbd.shmemvaddr);

    out->text_offset = 0;
    out->text_length = convert_to_utf16(outtxt, 1024, text);

    s->services.apt.nextparam.appid = APPID_SWKBD;
    s->services.apt.nextparam.cmd = APTCMD_WAKEUP;
    s->services.apt.nextparam.kobj = nullptr;
    s->services.apt.nextparam.paramsize = sizeof(SwkbdState);
    event_signal(s, &s->services.apt.resume_event);
}
