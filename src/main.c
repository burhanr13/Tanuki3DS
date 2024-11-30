#include <SDL2/SDL.h>
#include <stdio.h>

#include "3ds.h"
#include "emulator.h"
#include "pica/renderer_gl.h"

char wintitle[200];

#ifdef GLDEBUGCTX
void glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity,
                   GLsizei length, const char* message, const void* userParam) {
    printfln("[GLDEBUG]%d %d %d %d %s", source, type, id, severity, message);
}
#endif

int main(int argc, char** argv) {

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
    SDL_Window* window =
        SDL_CreateWindow("ctremu", SDL_WINDOWPOS_UNDEFINED,
                         SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH * g_upscale,
                         2 * SCREEN_HEIGHT * g_upscale, SDL_WINDOW_OPENGL);

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
                hle3ds_run_frame(&ctremu.system);
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

            snprintf(wintitle, 199, "ctremu | %s | %.2lf FPS",
                     ctremu.romfilenodir, fps);
            SDL_SetWindowTitle(window, wintitle);
            prev_fps_update = cur_time;
            prev_fps_frame = frame;
        }
        prev_time = cur_time;
    }

    SDL_Quit();

    emulator_quit();

    return 0;
}
