#include "gui.h"

#include <SDL3/SDL.h>

#include "emulator.h"
#include "services/applets.h"

struct UIState uistate = {.menubar = true};

void draw_swkbd() {
    if (ctremu.needs_swkbd) igOpenPopup_Str("Input Text", 0);

    if (igBeginPopupModal("Input Text", nullptr,
                          ImGuiWindowFlags_AlwaysAutoResize)) {
        static char buf[100];
        if (igIsWindowAppearing()) {
            memset(buf, 0, sizeof buf);
            igSetKeyboardFocusHere(0);
        }

        igInputText("##swkbd input", buf, sizeof buf, 0, nullptr, nullptr);

        if (igButton("Ok", (ImVec2) {})) {
            swkbd_resp(&ctremu.system, buf);
            igCloseCurrentPopup();
        }
        igEndPopup();
    }
}

void config_input(char* name, int* val) {
    igTableNextRow(0, 0);
    igTableNextColumn();
    igText(name);
    igTableNextColumn();
    if (uistate.waiting_key == val) {
        igBeginDisabled(true);
        igButton("Press Key...", (ImVec2) {150, 0});
        igEndDisabled();
    } else {
        char buf[100];
        snprintf(buf, sizeof buf, "%s##%s", SDL_GetScancodeName(*val), name);
        if (igButton(buf, (ImVec2) {150, 0})) {
            uistate.waiting_key = val;
        }
    }
}

void draw_settings() {
    if (!uistate.settings) return;

    static enum {
        PANE_SYSTEM,
        PANE_VIDEO,
        PANE_AUDIO,
        PANE_INPUT,
        PANE_MAX
    } curPane = PANE_SYSTEM;

    static const char* pane_names[] = {"System", "Video", "Audio", "Input"};

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (igGetIO()->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    igSetNextWindowClass(&(ImGuiWindowClass) {
        .ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge});

    igSetNextWindowSize((ImVec2) {500, 400}, ImGuiCond_FirstUseEver);

    igBegin("Settings", &uistate.settings, flags);

    igPushStyleColor_Vec4(ImGuiCol_ChildBg,
                          *igGetStyleColorVec4(ImGuiCol_FrameBg));
    igBeginChild("sidebar", (ImVec2) {100, 0}, 0, 0);
    igSeparator();
    for (int i = 0; i < PANE_MAX; i++) {
        if (igSelectable(pane_names[i], curPane == i, 0, (ImVec2) {})) {
            curPane = i;
        }
        igSeparator();
    }
    igEndChild();
    igPopStyleColor(1);

    igSameLine(0, 5);

    igBeginChild("settings", (ImVec2) {}, 0, 0);
    igBeginChild("settings pane", (ImVec2) {0, -40}, 0, 0);

    switch (curPane) {
        case PANE_SYSTEM: {
            igSeparatorText("System");
            igInputText("Username", ctremu.username, sizeof ctremu.username, 0,
                        nullptr, nullptr);
            static const char* languages[] = {
                "Japanese", "English",    "French",  "German",
                "Italian",  "Spanish",    "Chinese", "Korean",
                "Dutch",    "Portuguese", "Russian", "Taiwanese",
            };
            igCombo("System Language", &ctremu.language, languages,
                    countof(languages), 0);

            static const char* regions[] = {
                "JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN",
            };
            igCombo("System Region", &ctremu.region, regions, countof(regions),
                    0);

            break;
        }
        case PANE_VIDEO: {
            igSeparatorText("Video");
            if (igCheckbox("VSync", &ctremu.vsync)) {
                if (ctremu.vsync) {
                    if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);
                } else {
                    SDL_GL_SetSwapInterval(0);
                }
            }
            igBeginDisabled(ctremu.initialized);
            igDragInt("Video Scale", &ctremu.videoscale, 0.1, 1, 10, nullptr,
                      0);
            igSetNextItemWidth(200);
            igDragInt("Software Vertex Shader Threads", &ctremu.vshthreads, 0.1,
                      0, MAX_VSH_THREADS, nullptr, 0);
            igEndDisabled();
            igCheckbox("Shader JIT", &ctremu.shaderjit);
            igCheckbox("Hardware Vertex Shaders", &ctremu.hwvshaders);
            igBeginDisabled(!ctremu.hwvshaders);
            igCheckbox("Safe Multiplication", &ctremu.safeShaderMul);
            igEndDisabled();
            igCheckbox("Use Ubershader", &ctremu.ubershader);
            igCheckbox("Hash Textures", &ctremu.hashTextures);
            break;
        }
        case PANE_AUDIO: {
            igSeparatorText("Audio");
            igCheckbox("Audio Sync", &ctremu.audiosync);
            igSliderFloat("Volume", &ctremu.volume, 0, 200, nullptr, 0);
            break;
        }
        case PANE_INPUT: {
            if (igBeginTabBar("input tabs", 0)) {
                if (igBeginTabItem("Keyboard Input", nullptr, 0)) {
                    igBeginChild("keyboard input panel", (ImVec2) {}, 0, 0);
                    igBeginTable("input config", 2,
                                 ImGuiTableFlags_BordersOuterV, (ImVec2) {}, 0);
                    config_input("A", &ctremu.inputmap.kb.a);
                    config_input("B", &ctremu.inputmap.kb.b);
                    config_input("X", &ctremu.inputmap.kb.x);
                    config_input("Y", &ctremu.inputmap.kb.y);
                    config_input("L", &ctremu.inputmap.kb.l);
                    config_input("R", &ctremu.inputmap.kb.r);
                    config_input("Start", &ctremu.inputmap.kb.start);
                    config_input("Select", &ctremu.inputmap.kb.select);
                    igTableNextRow(0, 0);
                    config_input("D-Pad Left", &ctremu.inputmap.kb.dl);
                    config_input("D-Pad Right", &ctremu.inputmap.kb.dr);
                    config_input("D-Pad Up", &ctremu.inputmap.kb.du);
                    config_input("D-Pad Down", &ctremu.inputmap.kb.dd);
                    igTableNextRow(0, 0);
                    config_input("Circle Pad Left", &ctremu.inputmap.kb.cl);
                    config_input("Circle Pad Right", &ctremu.inputmap.kb.cr);
                    config_input("Circle Pad Up", &ctremu.inputmap.kb.cu);
                    config_input("Circle Pad Down", &ctremu.inputmap.kb.cd);
                    config_input("Circle Pad Modifier",
                                 &ctremu.inputmap.kb.cmod);
                    igEndTable();
                    igSetNextItemWidth(200);
                    igSliderFloat("Circle Pad Modifier Scale",
                                  &ctremu.inputmap.kb.cmodscale, 0, 1, nullptr,
                                  0);
                    igEndChild();
                    igEndTabItem();
                }

                if (igBeginTabItem("Freecam", nullptr,
                                   ImGuiTabItemFlags_None)) {
                    igBeginChild("freecam input panel", (ImVec2) {}, 0, 0);
                    igBeginTable("freecam config", 2,
                                 ImGuiTableFlags_BordersOuterV, (ImVec2) {}, 0);
                    config_input("Move Forward", &ctremu.inputmap.freecam.mf);
                    config_input("Move Backward", &ctremu.inputmap.freecam.mb);
                    config_input("Move Left", &ctremu.inputmap.freecam.ml);
                    config_input("Move Right", &ctremu.inputmap.freecam.mr);
                    config_input("Move Up", &ctremu.inputmap.freecam.mu);
                    config_input("Move Down", &ctremu.inputmap.freecam.md);
                    igTableNextRow(0, 0);
                    config_input("Look Up", &ctremu.inputmap.freecam.lu);
                    config_input("Look Down", &ctremu.inputmap.freecam.ld);
                    config_input("Look Left", &ctremu.inputmap.freecam.ll);
                    config_input("Look Right", &ctremu.inputmap.freecam.lr);
                    config_input("Roll Left", &ctremu.inputmap.freecam.rl);
                    config_input("Roll Right", &ctremu.inputmap.freecam.rr);
                    igTableNextRow(0, 0);
                    config_input("Slow Modifier",
                                 &ctremu.inputmap.freecam.slow_mod);
                    config_input("Fast Modifier",
                                 &ctremu.inputmap.freecam.fast_mod);
                    igEndTable();
                    igEndChild();
                    igEndTabItem();
                }

                igEndTabBar();
            }
            break;
        }
        case PANE_MAX:
            break;
    }
    igEndChild();

    igSeparator();

    igBeginDisabled(ctremu.initialized);
    if (igButton("Reset All", (ImVec2) {})) {
        emulator_load_default_settings();
    }
    igEndDisabled();

    igSameLine(0, 5);

    if (igButton("Close", (ImVec2) {})) {
        uistate.settings = false;
    }

    igEndChild();

    igEnd();
}

int samplenum = 2000;
float samplerange = 0.25f;

void plot_samples(DSPSampHist* wave) {
    float samples[FIFO_MAX(wave[0])];
    for (int i = 0; i < 2; i++) {
        igPushID_Int(i);
        igTableNextColumn();
        FIFO_foreach_ring(it, wave[i]) {
            samples[it.i] = (float) *it.p / BIT(16);
        }
        igPlotLines_FloatPtr("##", samples + countof(samples) - samplenum,
                             samplenum, 0, nullptr, -samplerange, samplerange,
                             (ImVec2) {200, 50}, 4);
        igPopID();
    }
}

void draw_audioview() {
    if (!uistate.audioview) return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (igGetIO()->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    igSetNextWindowClass(&(ImGuiWindowClass) {
        .ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge});

    igSetNextWindowSize((ImVec2) {650, 400}, ImGuiCond_FirstUseEver);

    igBegin("DSP Audio Channels", &uistate.audioview, flags);

    igSliderInt("Sample Length", &samplenum, 0, FIFO_MAX(g_dsp_chn_hist[0][0]),
                nullptr, 0);

    igSliderFloat("Amplitude Range", &samplerange, 0, 1, nullptr, 0);

    igBeginChild("audioviewchild", (ImVec2) {0, -40}, 0, 0);

    igBeginTable("##audioview", 4,
                 ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                     ImGuiTableFlags_BordersOuterV,
                 (ImVec2) {}, 0);

    igTableNextRow(ImGuiTableRowFlags_Headers, 0);
    igTableNextColumn();
    igTableNextColumn();
    igText("Left");
    igTableNextColumn();
    igText("Right");
    igTableNextColumn();
    igText("Disabled");

    igTableNextRow(0, 0);
    igTableNextColumn();
    igText("Final Output");
    igPushID_Int(-1);
    plot_samples(g_dsp_hist);
    igPopID();
    igTableNextColumn();

    igTableNextRow(0, 0);

    for (int i = 0; i < DSP_CHANNELS; i++) {
        igPushID_Int(i);
        igTableNextRow(0, 0);
        igTableNextColumn();
        igText("Channel %d", i);
        plot_samples(g_dsp_chn_hist[i]);
        igTableNextColumn();
        bool dis = g_dsp_chn_disable & BIT(i);
        g_dsp_chn_disable &= ~BIT(i);
        igCheckbox("##", &dis);
        g_dsp_chn_disable |= dis << i;
        igPopID();
    }

    igEndTable();

    igEndChild();

    igSeparator();

    if (igButton("Enable All", (ImVec2) {})) {
        g_dsp_chn_disable = 0;
    }
    igSameLine(0, 5);
    if (igButton("Disable All", (ImVec2) {})) {
        g_dsp_chn_disable = ~0;
    }
    igSameLine(0, 5);
    if (igButton("Close", (ImVec2) {})) {
        uistate.audioview = false;
    }

    igEnd();
}

void draw_gui() {
    draw_swkbd();
    draw_settings();
    draw_audioview();
}
