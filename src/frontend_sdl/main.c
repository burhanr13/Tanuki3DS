#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "3ds.h"
#include "cpu.h"
#include "emulator.h"
#include "video/renderer_gl.h"

#ifdef _WIN32
#define realpath(a, b) _fullpath(b, a, 4096)
#endif

const char usage[] =
    R"(ctremu [options] [romfile]
-h -- print help
-l -- enable info logging
-sN -- upscale by N
)";

// we need to read cmdline before initing emu
bool log_arg;
int scale_arg;
char* romfile_arg;

SDL_Window* g_window;

SDL_JoystickID g_gamepad_id;
SDL_Gamepad* g_gamepad;

SDL_AudioStream* g_audio;

bool g_pending_reset;

#define FREECAM_SPEED 5.0
#define FREECAM_ROTATE_SPEED 0.02

#ifdef GLDEBUGCTX
void glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity,
                   GLsizei length, const char* message, const void* userParam) {
    ldebug("[GLDEBUG]%d %d %d %d %s", source, type, id, severity, message);
}
#endif

void read_args(int argc, char** argv) {
    char c;
    while ((c = getopt(argc, argv, "hlvs:")) != (char) -1) {
        switch (c) {
            case 'l':
                log_arg = true;
                break;
            case 's': {
                int scale = atoi(optarg);
                if (scale <= 0) eprintf("invalid scale factor");
                else scale_arg = scale;
                break;
            }
            case '?':
            case 'h':
            default:
                eprintf(usage);
                exit(0);
        }
    }
    argc -= optind;
    argv += optind;
    if (argc >= 1) {
        romfile_arg = realpath(argv[0], nullptr);
    }
}

void file_callback(void*, char** files, int n) {
    if (files && files[0]) {
        emulator_set_rom(files[0]);
        g_pending_reset = true;
    }
}

void load_rom_dialog() {
    SDL_DialogFileFilter filetypes = {
        .name = "3DS Applications",
        .pattern = "3ds;cci;cxi;app;elf;axf;3dsx",
    };

    ctremu.pause = true;
    SDL_ShowOpenFileDialog((SDL_DialogFileCallback) file_callback, nullptr,
                           g_window, &filetypes, 1, nullptr, false);
}

void hotkey_press(SDL_Keycode key) {
    switch (key) {
        case SDLK_F5:
            ctremu.pause = !ctremu.pause;
            break;
        case SDLK_TAB:
            ctremu.fastforward = !ctremu.fastforward;
            break;
        case SDLK_F1:
            g_pending_reset = true;
            break;
        case SDLK_F2:
            load_rom_dialog();
            break;
        case SDLK_F4:
            g_cpulog = !g_cpulog;
            break;
        case SDLK_F7:
            ctremu.freecam_enable = !ctremu.freecam_enable;
            glm_mat4_identity(ctremu.freecam_mtx);
            renderer_gl_update_freecam(&ctremu.system.gpu.gl);
            break;
        case SDLK_F6:
            ctremu.mute = !ctremu.mute;
            break;
        case SDLK_F10:
            ctremu.viewlayout = (ctremu.viewlayout + 1) % LAYOUT_MAX;
            break;
#ifdef AUDIO_DEBUG
        case SDLK_0 ... SDLK_9:
            g_dsp_chn_disable ^= BIT(key - SDLK_0);
            break;
#endif
        default:
            break;
    }
}

void update_input(E3DS* s, SDL_Gamepad* controller) {
    const bool* keys = SDL_GetKeyboardState(nullptr);

    PadState btn = {};
    int cx = 0;
    int cy = 0;

    if (!ctremu.freecam_enable) {
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

        cx = (keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * INT16_MAX;
        cy = (keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) * INT16_MAX;
    } else {
        float speed = FREECAM_SPEED;
        if (keys[SDL_SCANCODE_LSHIFT]) speed /= 20;
        if (keys[SDL_SCANCODE_RSHIFT]) speed *= 20;

        vec3 t = {};
        if (keys[SDL_SCANCODE_A]) {
            t[0] = speed;
        }
        if (keys[SDL_SCANCODE_D]) {
            t[0] = -speed;
        }
        if (keys[SDL_SCANCODE_F]) {
            t[1] = speed;
        }
        if (keys[SDL_SCANCODE_R]) {
            t[1] = -speed;
        }
        if (keys[SDL_SCANCODE_W]) {
            t[2] = speed;
        }
        if (keys[SDL_SCANCODE_S]) {
            t[2] = -speed;
        }

        mat4 r = GLM_MAT4_IDENTITY_INIT;
        if (keys[SDL_SCANCODE_DOWN]) {
            glm_rotate_make(r, FREECAM_ROTATE_SPEED, GLM_XUP);
        }
        if (keys[SDL_SCANCODE_UP]) {
            glm_rotate_make(r, -FREECAM_ROTATE_SPEED, GLM_XUP);
        }
        if (keys[SDL_SCANCODE_LEFT]) {
            glm_rotate_make(r, -FREECAM_ROTATE_SPEED, GLM_YUP);
        }
        if (keys[SDL_SCANCODE_RIGHT]) {
            glm_rotate_make(r, FREECAM_ROTATE_SPEED, GLM_YUP);
        }
        if (keys[SDL_SCANCODE_Q]) {
            glm_rotate_make(r, FREECAM_ROTATE_SPEED, GLM_ZUP);
        }
        if (keys[SDL_SCANCODE_E]) {
            glm_rotate_make(r, -FREECAM_ROTATE_SPEED, GLM_ZUP);
        }

        mat4 m;
        glm_translate_make(m, t);
        glm_mat4_mul(m, ctremu.freecam_mtx, ctremu.freecam_mtx);
        glm_mat4_mul(r, ctremu.freecam_mtx, ctremu.freecam_mtx);

        renderer_gl_update_freecam(&ctremu.system.gpu.gl);
    }

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
        x -= ctremu.screens[SCREEN_BOT].x;
        x = x * SCREEN_WIDTH_BOT / ctremu.screens[SCREEN_BOT].w;
        y -= ctremu.screens[SCREEN_BOT].y;
        y = y * SCREEN_HEIGHT / ctremu.screens[SCREEN_BOT].h;
        if (x < 0 || x >= SCREEN_WIDTH_BOT || y < 0 || y >= SCREEN_HEIGHT) {
            hid_update_touch(s, 0, 0, false);
        } else {
            hid_update_touch(s, x, y, true);
        }
    } else {
        hid_update_touch(s, 0, 0, false);
    }
}

void audio_callback(s16 (*samples)[2], u32 count) {
    if (ctremu.fastforward || ctremu.mute) return;
    SDL_PutAudioStreamData(g_audio, samples, count * 2 * sizeof(s16));
}

int main(int argc, char** argv) {
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "Tanuki3DS");

    read_args(argc, argv);

    const char* basepath = SDL_GetBasePath();

    chdir(basepath);

    FILE* fp;
    if ((fp = fopen("portable.txt", "r"))) {
        fclose(fp);
    } else {
        char* prefpath = SDL_GetPrefPath("", "Tanuki3DS");
        chdir(prefpath);
        SDL_free(prefpath);
    }

#ifdef REDIRECTSTDOUT
    freopen("ctremu.log", "w", stdout);
#endif

    emulator_init();

    bool log_old = g_infologs;
    if (log_arg) g_infologs = true;
    int scale_old = ctremu.videoscale;
    if (scale_arg) ctremu.videoscale = scale_arg;
    if (romfile_arg) {
        emulator_set_rom(romfile_arg);
        free(romfile_arg);
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef GLDEBUGCTX
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
    g_window = SDL_CreateWindow("Tanuki3DS", ctremu.windowW, ctremu.windowH,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);

    SDL_GLContext glcontext = SDL_GL_CreateContext(g_window);
    if (!glcontext) {
        SDL_Quit();
        lerror("could not create gl context");
        return 1;
    }

    gladLoadGLLoader((void*) SDL_GL_GetProcAddress);

#ifdef GLDEBUGCTX
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugOutput, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                          GL_TRUE);
#endif

    if (ctremu.vsync) {
        if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);
    } else {
        SDL_GL_SetSwapInterval(0);
    }

    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(g_window);

    SDL_AudioSpec as = {
        .format = SDL_AUDIO_S16,
        .channels = 2,
        .freq = SAMPLE_RATE,
    };
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &as,
                                        nullptr, nullptr);

    SDL_ResumeAudioStreamDevice(g_audio);
    ctremu.audio_cb = audio_callback;

    if (!ctremu.romfile) {
        load_rom_dialog();
    } else {
        g_pending_reset = true;
    }

    Uint64 prev_frame_time = SDL_GetTicksNS();
    Uint64 prev_fps_update = prev_frame_time;
    Uint64 prev_fps_frame = 0;
    const Uint64 frame_ticks = SDL_NS_PER_SECOND / FPS;
    Uint64 frame = 0;
    double avg_frame_time = 0;
    int avg_frame_time_ct = 0;

    ldebug("Tanuki3DS %s", EMUVERSION);

    ctremu.running = true;
    while (ctremu.running) {

        if (g_pending_reset) {
            g_pending_reset = false;
            if (emulator_reset()) {
                ctremu.pause = false;
            } else {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tanuki3DS",
                                         "ROM loading failed", g_window);
                ctremu.pause = true;
            }
            SDL_RaiseWindow(g_window);
            SDL_ClearAudioStream(g_audio);
        }

        if (setjmp(ctremu.exceptionJmp)) {
            emulator_reset();
            ctremu.pause = true;
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR, "Tanuki3DS",
                "A fatal error has occurred or the application has exited. "
                "Please see the log for details.",
                g_window);
        }

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
                    if (!g_gamepad) {
                        g_gamepad_id = e.gdevice.which;
                        g_gamepad = SDL_OpenGamepad(g_gamepad_id);
                    }
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (g_gamepad && e.gdevice.which == g_gamepad_id) {
                        g_gamepad = nullptr;
                    }
                    break;
                case SDL_EVENT_DROP_FILE:
                    emulator_set_rom(e.drop.data);
                    g_pending_reset = true;
                    break;
            }
        }

        SDL_GetWindowSizeInPixels(g_window, &ctremu.windowW, &ctremu.windowH);
        emulator_calc_viewports();

        if (!ctremu.pause) {

            update_input(&ctremu.system, g_gamepad);

            gpu_gl_start_frame(&ctremu.system.gpu);

            Uint64 frame_start = SDL_GetTicksNS();
            e3ds_run_frame(&ctremu.system);
            frame++;
            Uint64 frame_time = SDL_GetTicksNS() - frame_start;
            avg_frame_time += (double) frame_time / SDL_NS_PER_MS;
            avg_frame_time_ct++;

            render_gl_main(&ctremu.system.gpu.gl);

            SDL_GL_SwapWindow(g_window);
        }

        Uint64 elapsed = SDL_GetTicksNS() - prev_fps_update;
        if (!ctremu.pause && elapsed >= SDL_NS_PER_SECOND / 2) {
            prev_fps_update = SDL_GetTicksNS();

            double fps =
                (double) SDL_NS_PER_SECOND * (frame - prev_fps_frame) / elapsed;

            char* wintitle;
            asprintf(&wintitle, "Tanuki3DS %s | %s | %.2lf FPS, %.3lf ms",
                     EMUVERSION, ctremu.system.romimage.name, fps,
                     avg_frame_time / avg_frame_time_ct);
            SDL_SetWindowTitle(g_window, wintitle);
            free(wintitle);
            prev_fps_frame = frame;
            avg_frame_time = 0;
            avg_frame_time_ct = 0;
        }

        if (ctremu.fastforward && !ctremu.pause) {
            gpu_gl_start_frame(&ctremu.system.gpu);
            while (SDL_GetTicksNS() - prev_frame_time < frame_ticks) {
                Uint64 frame_start = SDL_GetTicksNS();
                e3ds_run_frame(&ctremu.system);
                frame++;
                Uint64 frame_time = SDL_GetTicksNS() - frame_start;
                avg_frame_time += (double) frame_time / SDL_NS_PER_MS;
                avg_frame_time_ct++;
            }
        } else {
            if (ctremu.audiosync && !ctremu.mute) {
                while (SDL_GetAudioStreamQueued(g_audio) > 100 * FRAME_SAMPLES)
                    SDL_Delay(1);
            } else if (!ctremu.vsync) {
                Uint64 elapsed = SDL_GetTicksNS() - prev_frame_time;
                Sint64 wait = frame_ticks - elapsed;
                if (wait > 0) {
                    SDL_DelayPrecise(wait);
                }
            }
        }

        prev_frame_time = SDL_GetTicksNS();
    }

    SDL_DestroyAudioStream(g_audio);

    SDL_GL_DestroyContext(glcontext);
    SDL_DestroyWindow(g_window);
    SDL_CloseGamepad(g_gamepad);

    SDL_Quit();

    g_infologs = log_old;
    ctremu.videoscale = scale_old;

    emulator_quit();

    return 0;
}
