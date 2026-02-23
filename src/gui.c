#include "gui.h"

#include <SDL3/SDL.h>

#include "emulator.h"
#include "services/applets.h"

struct UIState uistate = {.menubar = true};

void setup_gui_theme() {
    igGetStyle()->FontSizeBase = 15;
    ImFontAtlas_AddFontDefaultVector(igGetIO()->Fonts, nullptr);

    igGetStyle()->FrameBorderSize = 1;
    igGetStyle()->FramePadding = (ImVec2) {5, 5};
    igGetStyle()->FrameRounding = 5;
    igGetStyle()->GrabRounding = 5;
    igGetStyle()->ItemSpacing = (ImVec2) {5, 5};
    igGetStyle()->ItemInnerSpacing = (ImVec2) {5, 5};
    igGetStyle()->PopupRounding = 5;
    igGetStyle()->PopupBorderSize = 1;
    igGetStyle()->SeparatorTextAlign = (ImVec2) {0.5, 0.5};
    igGetStyle()->WindowPadding = (ImVec2) {10, 10};
    igGetStyle()->WindowRounding = 5;
    igGetStyle()->WindowBorderSize = 1;
    igGetStyle()->SelectableTextAlign = (ImVec2) {0.5, 0.5};

    igStyleColorsDark(nullptr);
}

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

    igSameLine(0, 0);

    igBeginChild("settings", (ImVec2) {},
                 ImGuiChildFlags_AlwaysUseWindowPadding, 0);
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

void draw_textureview() {
    if (!ctremu.initialized) uistate.textureview = false;
    if (!uistate.textureview) return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (igGetIO()->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    igSetNextWindowClass(&(ImGuiWindowClass) {
        .ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge});

    igSetNextWindowSize((ImVec2) {800, 700}, ImGuiCond_FirstUseEver);

    igBegin("GPU Texture Viewer", &uistate.textureview, flags);

    igBeginTabBar("##texturetabbar", 0);

    if (igBeginTabItem("Textures", nullptr, 0)) {

        static int curTex = 0;

        auto texcache = &ctremu.system.gpu.textures;

        igPushStyleColor_Vec4(ImGuiCol_ChildBg,
                              *igGetStyleColorVec4(ImGuiCol_FrameBg));
        igBeginChild("##list", (ImVec2) {200, 0}, 0, 0);

        for (int i = 0; i < TEX_MAX; i++) {
            char buf[100];

            if (BitVec_test(texcache->occupied, i)) {
                sprintf(buf, "Texture %d", i);
                igBeginDisabled(false);
            } else {
                sprintf(buf, "[Empty]##%d", i);
                igBeginDisabled(true);
            }
            if (igSelectable(buf, curTex == i, 0, (ImVec2) {})) {
                curTex = i;
            }
            igEndDisabled();
        }

        igEndChild();
        igPopStyleColor(1);

        igSameLine(0, 0);

        igBeginChild("##texture pane", (ImVec2) {},
                     ImGuiChildFlags_AlwaysUseWindowPadding, 0);

        if (BitVec_test(texcache->occupied, curTex)) {

            auto tex = &texcache->d[curTex];

            float w, h;
            if (tex->width > tex->height) {
                w = 512;
                h = (float) tex->height * 512 / tex->width;
            } else {
                h = 512;
                w = (float) tex->width * 512 / tex->height;
            }

            igImageWithBg((ImTextureRef) {0, tex->tex}, (ImVec2) {w, h},
                          (ImVec2) {0, 1}, (ImVec2) {1, 0},
                          (ImVec4) {0.25, 0.25, 0.25, 1},
                          (ImVec4) {1, 1, 1, 1});
            static const char* fmts[] = {
                "RGBA8888", "RGB888", "RGBA5551", "RGB565", "RGBA4444", "LA88",
                "RG88",     "L8",     "A8",       "IA44",   "I4",       "A4",
                "ETC1",     "ETC1A4", "???",      "???"};
            igText("Addr: %#x  Size: %dx%d  Format: %s", tex->paddr, tex->width,
                   tex->height, fmts[tex->fmt]);
            igText("Hash: %016llx", tex->hash);
        }
        igEndChild();

        igEndTabItem();
    }

    if (igBeginTabItem("Framebuffers", nullptr, 0)) {

        static int curFb = 0;

        auto fbcache = &ctremu.system.gpu.fbs;

        igPushStyleColor_Vec4(ImGuiCol_ChildBg,
                              *igGetStyleColorVec4(ImGuiCol_FrameBg));
        igBeginChild("##list", (ImVec2) {200, 0}, 0, 0);

        for (int i = 0; i < FB_MAX; i++) {
            char buf[100];

            if (BitVec_test(fbcache->occupied, i)) {
                sprintf(buf, "Framebuffer %d", i);
                igBeginDisabled(false);
            } else {
                sprintf(buf, "[Empty]##%d", i);
                igBeginDisabled(true);
            }
            if (igSelectable(buf, curFb == i, 0, (ImVec2) {})) {
                curFb = i;
            }
            igEndDisabled();
        }

        igEndChild();
        igPopStyleColor(1);

        igSameLine(0, 0);

        igBeginChild("##fb pane", (ImVec2) {},
                     ImGuiChildFlags_AlwaysUseWindowPadding, 0);

        if (BitVec_test(fbcache->occupied, curFb)) {

            auto fb = &fbcache->d[curFb];

            float w, h;
            if (fb->width > fb->height) {
                w = 512;
                h = (float) fb->height * 512 / fb->width;
            } else {
                h = 512;
                w = (float) fb->width * 512 / fb->height;
            }

            static int bufselect;
            igRadioButton_IntPtr("Color Buffer", &bufselect, 0);
            igSameLine(0, 0);
            igRadioButton_IntPtr("Depth Buffer", &bufselect, 1);

            if (bufselect == 0) {
                igImageWithBg((ImTextureRef) {0, fb->color_tex},
                              (ImVec2) {w, h}, (ImVec2) {0, 1}, (ImVec2) {1, 0},
                              (ImVec4) {0.25, 0.25, 0.25, 1},
                              (ImVec4) {1, 1, 1, 1});
            } else {
                igImageWithBg((ImTextureRef) {0, fb->depth_tex},
                              (ImVec2) {w, h}, (ImVec2) {0, 1}, (ImVec2) {1, 0},
                              (ImVec4) {0.25, 0.25, 0.25, 1},
                              (ImVec4) {1, 1, 1, 1});
            }
            static const char* fmts[] = {"RGBA8888", "RGB888",   "RGB565",
                                         "RGBA5551", "RGBA4444", "???",
                                         "???",      "???"};
            igText("Color Addr: %#x  Size: %dx%d  Format: %s", fb->color_paddr,
                   fb->width, fb->height, fmts[fb->color_fmt]);
            igText("Depth Addr: %#x", fb->depth_paddr);
        }
        igEndChild();

        igEndTabItem();
    }

    igEndTabBar();

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
    if (!ctremu.initialized) uistate.audioview = false;
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
    draw_textureview();
    draw_audioview();
}
