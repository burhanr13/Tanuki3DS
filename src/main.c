#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "3ds.h"
#include "emulator.h"
#include "gui.h"
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

SDL_Window* g_window;

SDL_JoystickID g_gamepad_id;
SDL_Gamepad* g_gamepad;

SDL_AudioStream* g_audio;
SDL_AudioStream* g_audio_input;

SDL_Camera* g_camera;

char* romfile_arg;

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
    while ((c = getopt(argc, argv, "hl")) != (char) -1) {
        switch (c) {
            case 'l':
                g_infologs = true;
                break;
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

void hotkey_press(SDL_Keycode key) {
    if (igGetIO()->WantCaptureKeyboard) return;
    switch (key) {
        case SDLK_F5:
            ctremu.pause = !ctremu.pause;
            break;
        case SDLK_TAB:
            ctremu.fastforward = !ctremu.fastforward;
            break;
        case SDLK_F1:
            ctremu.pending_reset = true;
            break;
        case SDLK_F2:
            load_rom_dialog();
            break;
        case SDLK_F3:
            uistate.settings = true;
            break;
        case SDLK_F7:
            ctremu.freecam_enable = !ctremu.freecam_enable;
            glm_mat4_identity(ctremu.freecam_mtx);
            renderer_gl_update_freecam(&ctremu.system.gpu.gl);
            break;
        case SDLK_F6:
            ctremu.mute = !ctremu.mute;
            break;
        case SDLK_F9:
            ctremu.swapscreens = !ctremu.swapscreens;
            break;
        case SDLK_F10:
            ctremu.viewlayout = (ctremu.viewlayout + 1) % LAYOUT_MAX;
            break;
        case SDLK_F11:
            ctremu.fullscreen = !ctremu.fullscreen;
            SDL_SetWindowFullscreen(g_window, ctremu.fullscreen);
            break;
        case SDLK_ESCAPE:
            uistate.menubar = !uistate.menubar;
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

void update_input() {
    if (igGetIO()->WantCaptureKeyboard || igGetIO()->WantCaptureMouse) return;

    const bool* keys = SDL_GetKeyboardState(nullptr);

    PadState btn = {};
    int cx = 0;
    int cy = 0;

    if (!ctremu.freecam_enable) {
        btn.a = keys[ctremu.inputmap.kb.a];
        btn.b = keys[ctremu.inputmap.kb.b];
        btn.x = keys[ctremu.inputmap.kb.x];
        btn.y = keys[ctremu.inputmap.kb.y];
        btn.l = keys[ctremu.inputmap.kb.l];
        btn.r = keys[ctremu.inputmap.kb.r];
        btn.start = keys[ctremu.inputmap.kb.start];
        btn.select = keys[ctremu.inputmap.kb.select];
        btn.up = keys[ctremu.inputmap.kb.du];
        btn.down = keys[ctremu.inputmap.kb.dd];
        btn.left = keys[ctremu.inputmap.kb.dl];
        btn.right = keys[ctremu.inputmap.kb.dr];

        cx = (keys[ctremu.inputmap.kb.cr] - keys[ctremu.inputmap.kb.cl]) *
             INT16_MAX;
        cy = (keys[ctremu.inputmap.kb.cu] - keys[ctremu.inputmap.kb.cd]) *
             INT16_MAX;
        if (keys[ctremu.inputmap.kb.cmod]) {
            cx *= ctremu.inputmap.kb.cmodscale;
            cy /= ctremu.inputmap.kb.cmodscale;
        }
    } else {
        float speed = FREECAM_SPEED;
        if (keys[ctremu.inputmap.freecam.slow_mod]) speed /= 20;
        if (keys[ctremu.inputmap.freecam.fast_mod]) speed *= 20;

        vec3 t = {};
        if (keys[ctremu.inputmap.freecam.ml]) {
            t[0] = speed;
        }
        if (keys[ctremu.inputmap.freecam.mr]) {
            t[0] = -speed;
        }
        if (keys[ctremu.inputmap.freecam.md]) {
            t[1] = speed;
        }
        if (keys[ctremu.inputmap.freecam.mu]) {
            t[1] = -speed;
        }
        if (keys[ctremu.inputmap.freecam.mf]) {
            t[2] = speed;
        }
        if (keys[ctremu.inputmap.freecam.mb]) {
            t[2] = -speed;
        }

        mat4 r = GLM_MAT4_IDENTITY_INIT;
        if (keys[ctremu.inputmap.freecam.ld]) {
            glm_rotate_make(r, FREECAM_ROTATE_SPEED, GLM_XUP);
        }
        if (keys[ctremu.inputmap.freecam.lu]) {
            glm_rotate_make(r, -FREECAM_ROTATE_SPEED, GLM_XUP);
        }
        if (keys[ctremu.inputmap.freecam.ll]) {
            glm_rotate_make(r, -FREECAM_ROTATE_SPEED, GLM_YUP);
        }
        if (keys[ctremu.inputmap.freecam.lr]) {
            glm_rotate_make(r, FREECAM_ROTATE_SPEED, GLM_YUP);
        }
        if (keys[ctremu.inputmap.freecam.rl]) {
            glm_rotate_make(r, FREECAM_ROTATE_SPEED, GLM_ZUP);
        }
        if (keys[ctremu.inputmap.freecam.rr]) {
            glm_rotate_make(r, -FREECAM_ROTATE_SPEED, GLM_ZUP);
        }

        mat4 m;
        glm_translate_make(m, t);
        glm_mat4_mul(m, ctremu.freecam_mtx, ctremu.freecam_mtx);
        glm_mat4_mul(r, ctremu.freecam_mtx, ctremu.freecam_mtx);

        renderer_gl_update_freecam(&ctremu.system.gpu.gl);
    }

    if (g_gamepad) {
        btn.a |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_EAST);
        btn.b |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        btn.x |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_NORTH);
        btn.y |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_WEST);
        btn.start |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_START);
        btn.select |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_BACK);
        btn.left |=
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        btn.right |=
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        btn.up |= SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        btn.down |=
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        btn.l |=
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        btn.r |=
            SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

        int x = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        if (abs(x) > abs(cx)) cx = x;
        int y = -SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        if (abs(y) > abs(cy)) cy = y;

        int tl = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        if (tl > INT16_MAX / 10) btn.l = 1;
        int tr = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (tr > INT16_MAX / 10) btn.r = 1;

        int rx = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        int ry = SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
        if (rx > INT16_MAX / 2) btn.right = 1;
        if (rx < -INT16_MAX / 2) btn.left = 1;
        if (ry > INT16_MAX / 2) btn.down = 1;
        if (ry < -INT16_MAX / 2) btn.up = 1;
    }

    btn.cup = cy > INT16_MAX / 2;
    btn.cdown = cy < INT16_MIN / 2;
    btn.cleft = cx < INT16_MIN / 2;
    btn.cright = cx > INT16_MAX / 2;

    float xf, yf;
    bool pressed =
        SDL_GetMouseState(&xf, &yf) & SDL_BUTTON_MASK(SDL_BUTTON_LEFT);

    if (g_gamepad) {
        if (SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
            pressed = true;
        }
    }
    int tx = xf, ty = yf;

    if (pressed) {
        tx -= ctremu.screens[SCREEN_BOT].x;
        tx = tx * SCREEN_WIDTH_BOT / ctremu.screens[SCREEN_BOT].w;
        ty -= ctremu.screens[SCREEN_BOT].y;
        ty = ty * SCREEN_HEIGHT / ctremu.screens[SCREEN_BOT].h;
        if (tx < 0 || tx >= SCREEN_WIDTH_BOT || ty < 0 || ty >= SCREEN_HEIGHT) {
            pressed = false;
            tx = 0;
            ty = 0;
        }
    } else {
        tx = 0;
        ty = 0;
    }

    float accel[3] = {};
    float gyro[3] = {};
    if (g_gamepad) {
        SDL_GetGamepadSensorData(g_gamepad, SDL_SENSOR_ACCEL, accel, 3);
        SDL_GetGamepadSensorData(g_gamepad, SDL_SENSOR_GYRO, gyro, 3);

        // measurements from 3ds
        // gyroscope
        // yaw left: +z
        // pitch down: +x
        // roll left: -y
        // accelerometer
        // resting: around 0, -500, 0
        // left: -x
        // up: -y
        // back: -z
        // clamp at 930

        const float accelClamp = 930;
        const float gravityRead = 500;
        const float accelScale = gravityRead / SDL_STANDARD_GRAVITY;

        accel[0] = glm_clamp(accel[0] * accelScale, -accelClamp, accelClamp);
        accel[1] = glm_clamp(accel[1] * -accelScale, -accelClamp, accelClamp);
        accel[2] = glm_clamp(accel[2] * accelScale, -accelClamp, accelClamp);

        const float gyroScale = 180 / M_PI * HID_GYRO_DPS_COEFF;
        gyro[0] *= -gyroScale;
        gyro[1] *= gyroScale;
        gyro[2] *= -gyroScale;
    }

    // hid updates inputs 4x per frame so we must simulate this
    // by calling each function 4 times
    for (int i = 0; i < 4; i++) {
        hid_update_pad(&ctremu.system, btn.w, cx, cy);
        hid_update_touch(&ctremu.system, tx, ty, pressed);
        hid_update_accel(&ctremu.system, accel[0], accel[1], accel[2]);
        hid_update_gyro(&ctremu.system, gyro[0], gyro[1], gyro[2]);
    }
}

void update_mic() {
    if (!ctremu.system.services.mic.sampling || !ctremu.micEnable) {
        if (!g_audio_input) return;
        SDL_ClearAudioStream(g_audio_input);
        SDL_PauseAudioStreamDevice(g_audio_input);
        return;
    }
    if (!g_audio_input) {
        SDL_AudioSpec as_in = {
            .format = SDL_AUDIO_S16,
            .channels = 1,
            .freq = SAMPLE_RATE,
        };
        g_audio_input = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &as_in, nullptr, nullptr);
    }
    if (SDL_AudioStreamDevicePaused(g_audio_input)) {
        SDL_AudioFormat fmts[] = {SDL_AUDIO_U8, SDL_AUDIO_S16, SDL_AUDIO_S8,
                                  SDL_AUDIO_S16};
        SDL_AudioSpec as = {
            .format = fmts[ctremu.system.services.mic.encoding & 3],
            .channels = 1,
            .freq = ctremu.system.services.mic.sampleRate,
        };
        SDL_SetAudioStreamFormat(g_audio_input, &as, &as);
        SDL_SetAudioStreamGain(g_audio_input,
                               (float) ctremu.system.services.mic.gain / 64);
        SDL_ResumeAudioStreamDevice(g_audio_input);
    }
    int buflen = SDL_GetAudioStreamAvailable(g_audio_input);
    u8 buf[buflen];
    SDL_GetAudioStreamData(g_audio_input, buf, buflen);
    mic_send_data(&ctremu.system, buf, buflen);
}

void update_cam() {
    if (!ctremu.system.services.cam.capturing) {
        return;
    }
    if (!g_camera) {
        SDL_CameraID* cams = SDL_GetCameras(nullptr);
        if (!cams || !cams[0]) return;
        SDL_CameraSpec cs = {
            .format = SDL_PIXELFORMAT_RGB565,
            .colorspace = SDL_COLORSPACE_RGB_DEFAULT,
            .width = 640,
            .height = 480,
            .framerate_numerator = 30,
            .framerate_denominator = 1,
        };
        g_camera = SDL_OpenCamera(cams[0], &cs);
    }

    auto image = SDL_AcquireCameraFrame(g_camera, nullptr);
    if (!image) return;
    auto scaled = SDL_ScaleSurface(image, ctremu.system.services.cam.width,
                                   ctremu.system.services.cam.height,
                                   SDL_SCALEMODE_LINEAR);
    if (!ctremu.system.services.cam.rgb) {
        auto imageYUV = SDL_ConvertSurfaceAndColorspace(
            scaled, SDL_PIXELFORMAT_YUY2, nullptr, SDL_COLORSPACE_YUV_DEFAULT,
            0);
        SDL_DestroySurface(scaled);
        scaled = imageYUV;
    }
    cam_send_data(&ctremu.system, scaled->pixels);
    SDL_DestroySurface(scaled);
    SDL_ReleaseCameraFrame(g_camera, image);
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

    if (romfile_arg) {
        emulator_set_rom(romfile_arg);
        free(romfile_arg);
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD |
             SDL_INIT_CAMERA);

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

    igCreateContext(nullptr);
    igGetIO()->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    igGetIO()->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    igGetIO()->ConfigViewportsNoDecoration = false;
    ImGui_ImplSDL3_InitForOpenGL(g_window, glcontext);
    ImGui_ImplOpenGL3_Init(nullptr);

    setup_gui_theme();

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

        int exc;
        if ((exc = setjmp(ctremu.exceptionJmp))) {
            emulator_set_rom(nullptr);
            static const char* errmess[] = {
                [EXC_MEM] = "Invalid memory access.",
                [EXC_EXIT] = "The application has exited.",
                [EXC_ERRF] = "The application has reported a fatal error.",
                [EXC_BREAK] = "The application has terminated.",
            };
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tanuki3DS",
                                     errmess[exc], g_window);
        }

        if (ctremu.pending_reset) {
            ctremu.pending_reset = false;
            if (emulator_reset()) {
                ctremu.pause = false;
                uistate.menubar = !ctremu.initialized;
            } else {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tanuki3DS",
                                         "ROM loading failed", g_window);
                ctremu.pause = true;
                uistate.menubar = true;
            }
            SDL_RaiseWindow(g_window);
            SDL_ClearAudioStream(g_audio);
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            bool forward_imgui = true;
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    ctremu.running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (uistate.waiting_key) {
                        *uistate.waiting_key = e.key.scancode;
                        uistate.waiting_key = nullptr;
                        forward_imgui = false;
                    } else {
                        hotkey_press(e.key.key);
                    }
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (!g_gamepad) {
                        g_gamepad_id = e.gdevice.which;
                        g_gamepad = SDL_OpenGamepad(g_gamepad_id);
                        SDL_SetGamepadSensorEnabled(g_gamepad, SDL_SENSOR_ACCEL,
                                                    true);
                        SDL_SetGamepadSensorEnabled(g_gamepad, SDL_SENSOR_GYRO,
                                                    true);
                    }
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (g_gamepad && e.gdevice.which == g_gamepad_id) {
                        g_gamepad = nullptr;
                    }
                    break;
                case SDL_EVENT_DROP_FILE:
                    emulator_set_rom(e.drop.data);
                    break;
            }
            if (forward_imgui) ImGui_ImplSDL3_ProcessEvent(&e);
        }

        if (!ctremu.initialized) ctremu.pause = true;

        SDL_GetWindowSizeInPixels(g_window, &ctremu.windowW, &ctremu.windowH);
        emulator_calc_viewports();

        if (!ctremu.pause) {
            update_input();
            update_mic();
            update_cam();

            gpu_gl_start_frame(&ctremu.system.gpu);

            Uint64 frame_start = SDL_GetTicksNS();
            e3ds_run_frame(&ctremu.system);
            frame++;
            Uint64 frame_time = SDL_GetTicksNS() - frame_start;
            avg_frame_time += (double) frame_time / SDL_NS_PER_MS;
            avg_frame_time_ct++;

            render_gl_main(&ctremu.system.gpu.gl);
        }
        if (ctremu.initialized) {
            render_gl_main(&ctremu.system.gpu.gl);
        } else {
            glClear(GL_COLOR_BUFFER_BIT);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        igNewFrame();

        draw_menubar();

        if (!ctremu.initialized) draw_gamelist();

        draw_gui();

        igRender();
        ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());

        if (igGetIO()->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            igUpdatePlatformWindows();
            igRenderPlatformWindowsDefault(nullptr, nullptr);
            SDL_GL_MakeCurrent(g_window, glcontext);
        }

        SDL_GL_SwapWindow(g_window);

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
        } else if (!ctremu.initialized) {
            char* wintitle;
            asprintf(&wintitle, "Tanuki3DS %s", EMUVERSION);
            SDL_SetWindowTitle(g_window, wintitle);
            free(wintitle);
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
            if (ctremu.audiosync && !ctremu.mute && ctremu.initialized &&
                SDL_GetAudioStreamQueued(g_audio) != 0) {
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    igDestroyContext(nullptr);

    if (g_camera) SDL_CloseCamera(g_camera);

    if (g_audio_input) SDL_DestroyAudioStream(g_audio_input);
    SDL_DestroyAudioStream(g_audio);

    SDL_GL_DestroyContext(glcontext);
    SDL_DestroyWindow(g_window);
    SDL_CloseGamepad(g_gamepad);

    SDL_Quit();

    emulator_quit();

    return 0;
}
