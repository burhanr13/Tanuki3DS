#include <SDL3/SDL.h>
#include <stdio.h>
#include <unistd.h>

#include "3ds.h"
#include "emulator.h"
#include "pica/renderer_gl.h"

#ifdef GLDEBUGCTX
void glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity,
                   GLsizei length, const char* message, const void* userParam) {
    printfln("[GLDEBUG]%d %d %d %d %s", source, type, id, severity, message);
}
#endif

void hotkey_press(SDL_Keycode key) {
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

void update_input(E3DS* s, SDL_Gamepad* controller, int view_w, int view_h) {
    const bool* keys = SDL_GetKeyboardState(nullptr);

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
        btn.a |= SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_EAST);
        btn.b |= SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_SOUTH);
        btn.x |= SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_NORTH);
        btn.y |= SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_WEST);
        btn.start |= SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_START);
        btn.select |= SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_BACK);
        btn.left |=
            SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        btn.right |=
            SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        btn.up |= SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_DPAD_UP);
        btn.down |=
            SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_DPAD_DOWN);

        int x = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTX);
        if (abs(x) > abs(cx)) cx = x;
        int y = -SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTY);
        if (abs(y) > abs(cy)) cy = y;

        int tl = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        if (tl > INT16_MAX / 10) btn.l = 1;
        int tr = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (tr > INT16_MAX / 10) btn.r = 1;
    }

    btn.cup = cy > INT16_MAX / 2;
    btn.cdown = cy < INT16_MIN / 2;
    btn.cleft = cx < INT16_MIN / 2;
    btn.cright = cx > INT16_MAX / 2;

    hid_update_pad(s, btn.w, cx, cy);

    float xf, yf;
    bool pressed =
        SDL_GetMouseState(&xf, &yf) & SDL_BUTTON_MASK(SDL_BUTTON_LEFT);
    if (controller) {
        if (SDL_GetGamepadButton(controller,
                                 SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
            pressed = true;
        }
    }
    int x = xf, y = yf;

    if (pressed) {
        x -= view_w * (SCREEN_WIDTH - SCREEN_WIDTH_BOT) / (2 * SCREEN_WIDTH);
        x = x * SCREEN_WIDTH / view_w;
        y -= view_h / 2;
        y = y * 2 * SCREEN_HEIGHT / view_h;
        if (x < 0 || x >= SCREEN_WIDTH_BOT || y < 0 || y >= SCREEN_HEIGHT) {
            hid_update_touch(s, 0, 0, false);
        } else {
            hid_update_touch(s, x, y, true);
        }
    } else {
        hid_update_touch(s, 0, 0, false);
    }
}

void file_callback(bool* done, char** files, int n) {
    if (files && files[0]) ctremu.romfile = strdup(files[0]);
    *done = true;
}

int main(int argc, char** argv) {
    emulator_read_args(argc, argv);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef GLDEBUGCTX
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
    SDL_Window* window =
        SDL_CreateWindow(EMUNAME, SCREEN_WIDTH * ctremu.videoscale,
                         2 * SCREEN_HEIGHT * ctremu.videoscale,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    SDL_GLContext glcontext = SDL_GL_CreateContext(window);
    if (!glcontext) {
        SDL_Quit();
        lerror("could not create gl context");
        return 1;
    }
    glewInit();

#ifdef GLDEBUGCTX
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugOutput, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                          GL_TRUE);
#endif

    SDL_Gamepad* controller = nullptr;

    if (!ctremu.romfile) {
        SDL_DialogFileFilter filetypes = {.name = "3DS Executables",
                                          .pattern = "3ds;cci;cxi;app;elf"};
        bool done = false;
        SDL_PumpEvents();
        SDL_ShowOpenFileDialog((SDL_DialogFileCallback) file_callback, &done,
                               window, &filetypes, 1, nullptr, false);
        while (!done) {
            SDL_Event e;
            SDL_WaitEvent(&e);
            if (e.type == SDL_EVENT_QUIT) break;
        }
        if (!ctremu.romfile) {
            lerror("no file provided");
            exit(1);
        }
    }

    SDL_RaiseWindow(window);

#ifdef NOPORTABLE
    char* prefpath = SDL_GetPrefPath("", "Tanuki3DS");
    chdir(prefpath);
    SDL_free(prefpath);
#endif

    if (emulator_init() < 0) {
        SDL_Quit();
        return -1;
    }

    Uint64 prev_time = SDL_GetTicksNS();
    Uint64 prev_fps_update = prev_time;
    Uint64 prev_fps_frame = 0;
    const Uint64 frame_ticks = SDL_NS_PER_SECOND / FPS;
    Uint64 frame = 0;

    ctremu.running = true;
    while (ctremu.running) {
        Uint64 cur_time;
        Uint64 elapsed;

        if (!(ctremu.pause)) {
            do {
                e3ds_run_frame(&ctremu.system);
                frame++;

                cur_time = SDL_GetTicksNS();
                elapsed = cur_time - prev_time;
            } while (ctremu.uncap && elapsed < frame_ticks);
        }

        float aspect = (float) SCREEN_WIDTH / (2 * SCREEN_HEIGHT);
        SDL_SetWindowAspectRatio(window, aspect, aspect);
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);

        render_gl_main(&ctremu.system.gpu.gl, w, h);

        SDL_GL_SwapWindow(window);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    ctremu.running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    hotkey_press(e.key.key);
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (!controller)
                        controller = SDL_OpenGamepad(e.gdevice.which);
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    controller = nullptr;
                    break;
            }
        }

        update_input(&ctremu.system, controller, w, h);

        if (!ctremu.uncap) {
            cur_time = SDL_GetTicksNS();
            elapsed = cur_time - prev_time;
            Sint64 wait = frame_ticks - elapsed;
            if (wait > 0) {
                SDL_DelayPrecise(wait);
            }
        }
        cur_time = SDL_GetTicksNS();
        elapsed = cur_time - prev_fps_update;
        if (!ctremu.pause && elapsed >= SDL_NS_PER_SECOND / 2) {
            double fps =
                (double) SDL_NS_PER_SECOND * (frame - prev_fps_frame) / elapsed;

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

    SDL_GL_DestroyContext(glcontext);
    SDL_DestroyWindow(window);
    SDL_CloseGamepad(controller);

    SDL_Quit();

    emulator_quit();

    return 0;
}
