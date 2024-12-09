#include <SDL2/SDL.h>
#include <stdio.h>
#include "tinyfiledialogs/tinyfiledialogs.h"

#include "3ds.h"
#include "emulator.h"
#include "pica/renderer_gl.h"

#ifdef GLDEBUGCTX
void glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity,
                   GLsizei length, const char* message, const void* userParam) {
    printfln("[GLDEBUG]%d %d %d %d %s", source, type, id, severity, message);
}
#endif

void hotkey_press(SDL_KeyCode key) {
    switch (key) {
        case SDLK_F5:
            ctremu.pause = !ctremu.pause;
            break;
        case SDLK_TAB:
            ctremu.uncap = !ctremu.uncap;
            break;
        default:
            break;
    }
}

void update_input(E3DS* s, SDL_GameController* controller) {
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

    int cx = (keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * INT16_MAX;
    int cy = (keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) * INT16_MAX;

    if (controller) {
        btn.a |=
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B);
        btn.b |=
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A);
        btn.x |=
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y);
        btn.y |=
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X);
        btn.l |= SDL_GameControllerGetButton(
            controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        btn.r |= SDL_GameControllerGetButton(
            controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        btn.start |= SDL_GameControllerGetButton(controller,
                                                 SDL_CONTROLLER_BUTTON_START);
        btn.select |=
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
        btn.left |= SDL_GameControllerGetButton(
            controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        btn.right |= SDL_GameControllerGetButton(
            controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        btn.up |= SDL_GameControllerGetButton(controller,
                                              SDL_CONTROLLER_BUTTON_DPAD_UP);
        btn.down |= SDL_GameControllerGetButton(
            controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);

        int x =
            SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        if (abs(x) > abs(cx)) cx = x;
        int y =
            -SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        if (abs(y) > abs(cy)) cy = y;
    }

    btn.cup = cy > INT16_MAX / 2;
    btn.cdown = cy < INT16_MIN / 2;
    btn.cleft = cx < INT16_MIN / 2;
    btn.cright = cx > INT16_MAX / 2;

    hid_update_pad(s, btn.w, cx, cy);

    int x, y;
    bool pressed = SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_LEFT);

    if (pressed) {
        x -= (SCREEN_WIDTH - SCREEN_WIDTH_BOT) / 2 * ctremu.videoscale;
        x /= ctremu.videoscale;
        y -= SCREEN_HEIGHT * ctremu.videoscale;
        y /= ctremu.videoscale;
        if (x < 0 || x >= SCREEN_WIDTH_BOT || y < 0 || y >= SCREEN_HEIGHT) {
            hid_update_touch(s, 0, 0, false);
        } else {
            hid_update_touch(s, x, y, true);
        }
    } else {
        hid_update_touch(s, 0, 0, false);
    }
}

int main(int argc, char** argv) {

    emulator_read_args(argc, argv);
#ifdef USE_TFD
    if (!ctremu.romfile) {
        const char* filetypes[] = {"*.3ds", "*.cci", "*.cxi", "*.app", "*.elf"};
        ctremu.romfile = tinyfd_openFileDialog(
            EMUNAME ": Open Game", NULL, sizeof filetypes / sizeof filetypes[0],
            filetypes, "3DS Executables", false);
    }
#endif

    if (emulator_init(argc, argv) < 0) return -1;

    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

    SDL_GameController* controller = NULL;
    if (SDL_NumJoysticks() > 0) {
        controller = SDL_GameControllerOpen(0);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef GLDEBUGCTX
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
    SDL_Window* window = SDL_CreateWindow(
        EMUNAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH * ctremu.videoscale, 2 * SCREEN_HEIGHT * ctremu.videoscale,
        SDL_WINDOW_OPENGL);

    SDL_GLContext glcontext = SDL_GL_CreateContext(window);
    if (!glcontext) {
        SDL_Quit();
        return 1;
    }
    glewInit();

#ifdef GLDEBUGCTX
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugOutput, NULL);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL,
                          GL_TRUE);
#endif

    renderer_gl_setup(&ctremu.system.gpu.gl, &ctremu.system.gpu);

    Uint64 prev_time = SDL_GetPerformanceCounter();
    Uint64 prev_fps_update = prev_time;
    Uint64 prev_fps_frame = 0;
    const Uint64 frame_ticks = SDL_GetPerformanceFrequency() / FPS;
    Uint64 frame = 0;

    ctremu.running = true;
    while (ctremu.running) {
        Uint64 cur_time;
        Uint64 elapsed;

        if (!(ctremu.pause)) {
            do {
                e3ds_run_frame(&ctremu.system);
                frame++;

                cur_time = SDL_GetPerformanceCounter();
                elapsed = cur_time - prev_time;
            } while (ctremu.uncap && elapsed < frame_ticks);
        }

        render_gl_main(&ctremu.system.gpu.gl);

        SDL_GL_SwapWindow(window);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                ctremu.running = false;
                break;
            }
            if (e.type == SDL_KEYDOWN) {
                hotkey_press(e.key.keysym.sym);
            }
        }

        update_input(&ctremu.system, controller);

        if (!ctremu.uncap) {
            cur_time = SDL_GetPerformanceCounter();
            elapsed = cur_time - prev_time;
            Sint64 wait = frame_ticks - elapsed;
            Sint64 waitMS =
                wait * 1000 / (Sint64) SDL_GetPerformanceFrequency();
            if (waitMS > 0) {
                SDL_Delay(waitMS);
            }
        }
        cur_time = SDL_GetPerformanceCounter();
        elapsed = cur_time - prev_fps_update;
        if (!ctremu.pause && elapsed >= SDL_GetPerformanceFrequency() / 2) {
            double fps = (double) SDL_GetPerformanceFrequency() *
                         (frame - prev_fps_frame) / elapsed;

            char* wintitle;
            asprintf(&wintitle, EMUNAME " | %s | %.2lf FPS",
                     ctremu.romfilenodir, fps);
            SDL_SetWindowTitle(window, wintitle);
            free(wintitle);
            prev_fps_update = cur_time;
            prev_fps_frame = frame;
        }
        prev_time = cur_time;
    }

    SDL_Quit();

    emulator_quit();

    return 0;
}
