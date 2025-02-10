#include "applets.h"

void swkbd_run(E3DS* s, u32 paramsvaddr, u32 shmemvaddr) {

    linfo("running swkbd");

    SwkbdState* in = PTR(paramsvaddr);
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

    u16* outtxt = PTR(shmemvaddr);

    char text[] = "lmao";

    out->text_offset = 0;
    out->text_length = sizeof text;
    for (int i = 0; i < sizeof text; i++) {
        outtxt[i] = text[i];
    }

    s->services.apt.nextparam.appid = APPID_SWKBD;
    s->services.apt.nextparam.cmd = APTCMD_WAKEUP;
    s->services.apt.nextparam.kobj = nullptr;
    s->services.apt.nextparam.paramsize = sizeof(SwkbdState);
    event_signal(s, &s->services.apt.resume_event);
}