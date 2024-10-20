#include "emulator.h"

#include <SDL2/SDL.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "3ds.h"
#include "arm/arm.h"
#include "arm/thumb.h"
#include "emulator_state.h"
#include "services/hid.h"

EmulatorState ctremu;

const char usage[] = "ctremu [options] <romfile>\n"
                     "-h -- print help\n";

int emulator_init(int argc, char** argv) {
    read_args(argc, argv);
    if (!ctremu.romfile) {
        eprintf(usage);
        return -1;
    }

    arm_generate_lookup();
    thumb_generate_lookup();

    emulator_reset();

    ctremu.romfilenodir = strrchr(ctremu.romfile, '/');
    if (ctremu.romfilenodir) ctremu.romfilenodir++;
    else ctremu.romfilenodir = ctremu.romfile;
    return 0;
}

void emulator_quit() {
    hle3ds_destroy(&ctremu.system);
}

void emulator_reset() {
    if (ctremu.initialized) {
        hle3ds_destroy(&ctremu.system);
    }

    hle3ds_init(&ctremu.system, ctremu.romfile);

    ctremu.initialized = true;
}

void read_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (char* f = &argv[i][1]; *f; f++) {
                switch (*f) {
                    case 'h':
                        eprintf(usage);
                        exit(0);
                    default:
                        eprintf("Invalid argument\n");
                }
            }
        } else {
            ctremu.romfile = argv[i];
        }
    }
}

void hotkey_press(SDL_KeyCode key) {
    switch (key) {
        case SDLK_p:
            ctremu.pause = !ctremu.pause;
            break;
        case SDLK_m:
            ctremu.mute = !ctremu.mute;
            break;
        case SDLK_r:
            emulator_reset();
            ctremu.pause = false;
            break;
        case SDLK_TAB:
            ctremu.uncap = !ctremu.uncap;
            break;
        default:
            break;
    }
}

void update_input(HLE3DS* s) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);

    PadState btn;
    btn.a = keys[SDL_SCANCODE_L];
    btn.b = keys[SDL_SCANCODE_K];
    btn.x = keys[SDL_SCANCODE_O];
    btn.y = keys[SDL_SCANCODE_I];
    btn.l = keys[SDL_SCANCODE_Q];
    btn.r = keys[SDL_SCANCODE_P];
    btn.start = keys[SDL_SCANCODE_RETURN];
    btn.select = keys[SDL_SCANCODE_RSHIFT];
    btn.up = keys[SDL_SCANCODE_UP];
    btn.down = keys[SDL_SCANCODE_DOWN];
    btn.left = keys[SDL_SCANCODE_LEFT];
    btn.right = keys[SDL_SCANCODE_RIGHT];
    btn.cup = keys[SDL_SCANCODE_W];
    btn.cdown = keys[SDL_SCANCODE_S];
    btn.cleft = keys[SDL_SCANCODE_A];
    btn.cright = keys[SDL_SCANCODE_D];

    s16 cx = (btn.cright - btn.cleft) * INT16_MAX;
    s16 cy = (btn.cup - btn.cdown) * INT16_MAX;

    hid_update_pad(s, btn.w, cx, cy);
}