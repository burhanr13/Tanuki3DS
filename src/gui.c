#include "gui.h"

#include <SDL3/SDL.h>
#include <dirent.h>
#include <unistd.h>

#include <imgui/dcimgui.h>

#include "arm/jit/jit.h"
#include "cpu.h"
#include "emulator.h"
#include "kernel/loader.h"
#include "services/applets.h"
#include "unicode.h"

#ifdef __linux__
#define OPEN_CMD "xdg-open"
#else
#define OPEN_CMD "open"
#endif

#ifdef _WIN32
#define fseek(a, b, c) _fseeki64(a, b, c)
#define ftell(a) _ftelli64(a)
#endif

struct UIState uistate = {.menubar = true};

extern SDL_Window* g_window;

void file_callback(void*, const char* const* files, int) {
    if (files && files[0]) {
        emulator_set_rom(files[0]);
    }
}

void load_rom_dialog() {
    SDL_DialogFileFilter filetypes = {
        .name = "3DS Applications",
        .pattern = "3ds;cci;cxi;app;elf;axf;3dsx",
    };

    SDL_ShowOpenFileDialog(file_callback, nullptr, g_window, &filetypes, 1,
                           nullptr, false);
}

void load_sysfile_callback(void* dstfile, const char* const* files, int) {
    if (files && files[0]) {
        char* cmd;
        asprintf(&cmd, "cp '%s' 3ds/sys_files/%s", files[0], (char*) dstfile);
        system(cmd);
        free(cmd);
    }
}

void load_sysfile_dialog(char* dstfile) {
    SDL_ShowOpenFileDialog(load_sysfile_callback, dstfile, g_window, nullptr, 0,
                           nullptr, false);
}

void setup_gui_theme() {
    ImGui_GetStyle()->FontSizeBase = 15;
    ImFontAtlas_AddFontDefaultVector(ImGui_GetIO()->Fonts, nullptr);

    ImGui_GetStyle()->FrameBorderSize = 1;
    ImGui_GetStyle()->FramePadding = (ImVec2) {5, 5};
    ImGui_GetStyle()->FrameRounding = 5;
    ImGui_GetStyle()->GrabRounding = 5;
    ImGui_GetStyle()->ItemSpacing = (ImVec2) {5, 5};
    ImGui_GetStyle()->ItemInnerSpacing = (ImVec2) {5, 5};
    ImGui_GetStyle()->PopupRounding = 5;
    ImGui_GetStyle()->PopupBorderSize = 1;
    ImGui_GetStyle()->SeparatorTextAlign = (ImVec2) {0.5, 0.5};
    ImGui_GetStyle()->WindowPadding = (ImVec2) {10, 10};
    ImGui_GetStyle()->WindowRounding = 5;
    ImGui_GetStyle()->WindowBorderSize = 1;

    ImGui_StyleColorsDark(nullptr);
}

void draw_menubar() {
    if (!uistate.menubar) return;

    if (ImGui_BeginMainMenuBar()) {
        if (ImGui_BeginMenu("File")) {
            if (ImGui_MenuItemEx("Open", "F2", false, true)) {
                load_rom_dialog();
            }
            if (ImGui_BeginMenuEx("Open Recent", ctremu.history[0])) {
                for (int i = 0; i < HISTORYLEN; i++) {
                    if (!ctremu.history[i]) break;
                    if (ImGui_MenuItem(ctremu.history[i])) {
                        emulator_set_rom(ctremu.history[i]);
                    }
                }
                ImGui_EndMenu();
            }
            ImGui_Separator();

            if (ImGui_BeginMenu("Load System File")) {
                if (ImGui_MenuItem("Shared Font")) {
                    load_sysfile_dialog("font.bcfnt");
                }
                if (ImGui_MenuItem("Mii Data")) {
                    load_sysfile_dialog("mii.app.romfs");
                }
                ImGui_EndMenu();
            }

            if (ImGui_MenuItem("Open Tanuki3DS Folder")) {
                char* cwd = getcwd(nullptr, 0);
                char* cmd;
                asprintf(&cmd, OPEN_CMD " '%s'", cwd);
                system(cmd);
                free(cwd);
                free(cmd);
            }

            ImGui_Separator();
            if (ImGui_MenuItem("Exit")) {
                ctremu.running = false;
            }
            ImGui_EndMenu();
        }
        if (ImGui_BeginMenuEx("Emulation", ctremu.initialized)) {
            if (ImGui_MenuItemEx("Reset", "F1", false, true)) {
                ctremu.pending_reset = true;
            }
            ImGui_MenuItemBoolPtr("Pause", "F5", &ctremu.pause, true);
            if (ImGui_MenuItem("Stop")) {
                emulator_set_rom(nullptr);
            }
            ImGui_Separator();

            ImGui_MenuItemBoolPtr("Fast Forward", "Tab", &ctremu.fastforward,
                                  true);
            ImGui_MenuItemBoolPtr("Mute", "F6", &ctremu.mute, true);

            ImGui_Separator();

            if (ImGui_MenuItemBoolPtr("Free Camera", "F7",
                                      &ctremu.freecam_enable, true)) {
                glm_mat4_identity(ctremu.freecam_mtx);
                renderer_gl_update_freecam(&ctremu.system.gpu.gl);
            }

            ImGui_EndMenu();
        }

        if (ImGui_BeginMenu("View")) {
            if (ImGui_MenuItemBoolPtr("Fullscreen", "F11", &ctremu.fullscreen,
                                      true)) {
                SDL_SetWindowFullscreen(g_window, ctremu.fullscreen);
            }
            if (ImGui_BeginMenu("Screen Layout")) {
                if (ImGui_MenuItemEx("Vertical", nullptr,
                                     ctremu.viewlayout == LAYOUT_DEFAULT,
                                     true)) {
                    ctremu.viewlayout = LAYOUT_DEFAULT;
                }
                if (ImGui_MenuItemEx("Horizontal", nullptr,
                                     ctremu.viewlayout == LAYOUT_HORIZONTAL,
                                     true)) {
                    ctremu.viewlayout = LAYOUT_HORIZONTAL;
                }
                if (ImGui_MenuItemEx("Large Screen", nullptr,
                                     ctremu.viewlayout == LAYOUT_LARGETOP,
                                     true)) {
                    ctremu.viewlayout = LAYOUT_LARGETOP;
                }
                ImGui_Separator();
                ImGui_MenuItemBoolPtr("Swap Screens", "F9", &ctremu.swapscreens,
                                      true);
                ImGui_EndMenu();
            }
            ImGui_Separator();

            if (ImGui_MenuItemEx("Settings", "F3", false, true)) {
                uistate.settings = true;
            }

            ImGui_Separator();

            if (ImGui_MenuItemEx("Texture Viewer", nullptr, false,
                                 ctremu.initialized)) {
                uistate.textureview = true;
            }

            if (ImGui_MenuItemEx("Audio Channels", nullptr, false,
                                 ctremu.initialized)) {
                uistate.audioview = true;
            }

            ImGui_EndMenu();
        }

        if (ImGui_BeginMenuEx("Debug", ctremu.initialized)) {
            ImGui_MenuItemBoolPtr("Verbose Log", nullptr, &g_infologs, true);
            ImGui_MenuItemBoolPtr("CPU Trace Log", nullptr, &g_cpulog, true);
            ImGui_Separator();
            ImGui_MenuItemBoolPtr("Wireframe", nullptr, &g_wireframe, true);
            ImGui_EndMenu();
        }

        if (ImGui_BeginMenu("About")) {
            ImGui_TextLinkOpenURLEx("GitHub",
                                    "https://github.com/burhanr13/Tanuki3DS");
            ImGui_TextLinkOpenURLEx("Discord", "https://discord.gg/6ya65fvD3g");
            ImGui_EndMenu();
        }

        ImGui_Spacing();
        ImGui_TextDisabled("Esc to toggle menu bar");

        ImGui_EndMainMenuBar();
    }
}

struct GameEntry {
    char* filename;
    char gamename[128];
    char publisher[64];
    int region;
    size_t size;
    GLuint icontex;
};
bool gamelist_refresh = true;
Vec(struct GameEntry) gamelist;

void game_dir_callback(void*, const char* const* files, int) {
    if (files && files[0]) {
        free(ctremu.gamedir);
        ctremu.gamedir = strdup(files[0]);
        gamelist_refresh = true;
    }
}

int compar_game(struct GameEntry* a, struct GameEntry* b) {
    return strcmp(a->gamename, b->gamename);
}

void create_gamelist() {
    Vec_foreach(g, gamelist) {
        free(g->filename);
        glDeleteTextures(1, &g->icontex);
    }
    Vec_free(gamelist);
    gamelist_refresh = false;

    if (!ctremu.gamedir) return;

    DIR* dp = opendir(ctremu.gamedir);
    if (!dp) return;
    struct dirent* ent;
    while ((ent = readdir(dp))) {
        struct GameEntry g;
        asprintf(&g.filename, "%s/%s", ctremu.gamedir, ent->d_name);

        int smdhOff = find_smdh(g.filename);
        if (smdhOff == -1) continue;

        FILE* fp = fopen(g.filename, "rb");
        if (!fp) continue;

        fseek(fp, 0, SEEK_END);
        g.size = ftell(fp);

        fseek(fp, smdhOff, SEEK_SET);

        SMDHFile smdh;
        fread(&smdh, sizeof smdh, 1, fp);

        if (!strcmp(strrchr(g.filename, '.'), ".3dsx")) {
            convert_utf16(g.gamename, countof(g.gamename),
                          smdh.titles[1].shortname,
                          countof(smdh.titles[1].shortname));
        } else {
            convert_utf16(g.gamename, countof(g.gamename),
                          smdh.titles[1].longname,
                          countof(smdh.titles[1].longname));
        }
        for (int i = 0; i < countof(g.gamename); i++) {
            if (g.gamename[i] == '\n') g.gamename[i] = ' ';
        }
        convert_utf16(g.publisher, countof(g.publisher),
                      smdh.titles[1].publisher,
                      countof(smdh.titles[1].publisher));
        g.region = smdh.settings.regionLock;

        // icons generally stored 48x48 in rgb565 format, swizzled
        u16 iconraw[48 * 48];
        u16 icon[48][48];
        // skip smaller icon
        fseek(fp, 24 * 24 * 2, SEEK_CUR);
        fread(iconraw, sizeof(u16), 48 * 48, fp);
        for (int y = 0; y < 48; y++) {
            for (int x = 0; x < 48; x++) {
                icon[y][x] = iconraw[morton_swizzle(48, x, y)];
            }
        }
        glGenTextures(1, &g.icontex);
        glBindTexture(GL_TEXTURE_2D, g.icontex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 48, 48, 0, GL_RGB,
                     GL_UNSIGNED_SHORT_5_6_5, icon);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        fclose(fp);

        Vec_push(gamelist, g);
    }
    closedir(dp);

    qsort(gamelist.d, gamelist.size, sizeof(gamelist.d[0]),
          (void*) compar_game);
}

void draw_gamelist() {
    if (gamelist_refresh) create_gamelist();

    ImGui_SetNextWindowViewport(ImGui_GetMainViewport()->ID);
    ImGui_SetNextWindowPos(ImGui_GetMainViewport()->WorkPos, 0);
    ImGui_SetNextWindowSize(ImGui_GetMainViewport()->WorkSize, 0);
    ImGui_Begin("game list", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoSavedSettings);

    if (ImGui_Button("Select Game Folder...")) {
        SDL_ShowOpenFolderDialog(game_dir_callback, nullptr, g_window, nullptr,
                                 false);
    }
    ImGui_SameLine();
    if (ctremu.gamedir) {
        ImGui_Text("Current: %s", ctremu.gamedir);
    } else {
        ImGui_Text("Current: [None]");
    }
    ImGui_SameLine();
    if (ImGui_Button("Refresh")) {
        gamelist_refresh = true;
    }

    ImGui_BeginChild("gamelist_child", (ImVec2) {}, 0, 0);
    ImGui_BeginTable("gamelist", 5,
                     ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                         ImGuiTableFlags_ScrollY);

    ImGui_TableSetupScrollFreeze(0, 1);
    ImGui_TableSetupColumnEx("Icon", ImGuiTableColumnFlags_WidthFixed, 50, 0);
    ImGui_TableSetupColumn("Title", 0);
    ImGui_TableSetupColumn("Publisher", 0);
    ImGui_TableSetupColumn("Region", 0);
    ImGui_TableSetupColumn("Size", 0);
    ImGui_TableHeadersRow();

    Vec_foreach(g, gamelist) {
        ImGui_PushID(g->filename);
        ImGui_TableNextRow();
        ImGui_TableNextColumn();
        if (ImGui_SelectableEx("##", false,
                               ImGuiSelectableFlags_SpanAllColumns |
                                   ImGuiSelectableFlags_AllowOverlap |
                                   ImGuiSelectableFlags_AllowDoubleClick,
                               (ImVec2) {0, 48})) {
            if (ImGui_IsMouseDoubleClicked(0)) emulator_set_rom(g->filename);
        }
        ImGui_SameLine();
        ImGui_ImageEx((ImTextureRef) {0, g->icontex}, (ImVec2) {48, 48},
                      (ImVec2) {0, 0}, (ImVec2) {1, 1});
        ImGui_TableNextColumn();
        ImGui_Text("%s", g->gamename);
        ImGui_TextDisabled("%s", strrchr(g->filename, '/') + 1);
        ImGui_TableNextColumn();
        ImGui_Text("%s", g->publisher);
        ImGui_TableNextColumn();
        static const char* regions[] = {
            "JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN",
        };
        if ((g->region & 0x7f) == 0x7f) {
            ImGui_Text("Any");
        } else {
            ImGui_Text("%s", regions[__builtin_ctz(g->region)]);
        }
        ImGui_TableNextColumn();
        if (g->size < BIT(20)) {
            ImGui_Text("%.1f KiB", (double) g->size / BIT(10));
        } else if (g->size < BIT(30)) {
            ImGui_Text("%.1f MiB", (double) g->size / BIT(20));
        } else {
            ImGui_Text("%.1f GiB", (double) g->size / BIT(30));
        }
        ImGui_PopID();
    }

    ImGui_EndTable();
    ImGui_EndChild();

    ImGui_End();
}

void draw_swkbd() {
    if (ctremu.needs_swkbd) ImGui_OpenPopup("Input Text", 0);

    if (ImGui_BeginPopupModal("Input Text", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        static char buf[100];
        if (ImGui_IsWindowAppearing()) {
            memset(buf, 0, sizeof buf);
            ImGui_SetKeyboardFocusHere();
        }

        ImGui_InputText("##swkbd input", buf, sizeof buf, 0);

        if (ImGui_Button("Ok")) {
            swkbd_resp(&ctremu.system, buf);
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
    }
}

void config_input(char* name, int* val) {
    ImGui_TableNextRow();
    ImGui_TableNextColumn();
    ImGui_Text("%s", name);
    ImGui_TableNextColumn();
    if (uistate.waiting_key == val) {
        ImGui_BeginDisabled(true);
        ImGui_ButtonEx("Press Key...", (ImVec2) {150, 0});
        ImGui_EndDisabled();
    } else {
        char buf[100];
        snprintf(buf, sizeof buf, "%s##%s", SDL_GetScancodeName(*val), name);
        if (ImGui_ButtonEx(buf, (ImVec2) {150, 0})) {
            uistate.waiting_key = val;
        }
    }
}

void draw_settings() {
    if (!uistate.settings) return;

    static enum {
        PANE_SYSTEM,
        PANE_CPU,
        PANE_VIDEO,
        PANE_AUDIO,
        PANE_INPUT,
        PANE_MAX
    } curPane = PANE_SYSTEM;

    static const char* pane_names[] = {"System", "CPU", "Video", "Audio",
                                       "Input"};

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui_GetIO()->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    ImGui_SetNextWindowClass(&(ImGuiWindowClass) {
        .ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge});

    ImGui_SetNextWindowSize((ImVec2) {500, 400}, ImGuiCond_FirstUseEver);

    ImGui_Begin("Settings", &uistate.settings, flags);

    ImGui_PushStyleColorImVec4(ImGuiCol_ChildBg,
                               *ImGui_GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_SelectableTextAlign,
                             (ImVec2) {0.5, 0.5});
    ImGui_BeginChild("sidebar", (ImVec2) {100, 0}, 0, 0);
    ImGui_Separator();
    for (int i = 0; i < PANE_MAX; i++) {
        if (ImGui_SelectableEx(pane_names[i], curPane == i, 0, (ImVec2) {})) {
            curPane = i;
        }
        ImGui_Separator();
    }
    ImGui_EndChild();
    ImGui_PopStyleVar();
    ImGui_PopStyleColor();

    ImGui_SameLine();

    ImGui_BeginChild("settings", (ImVec2) {},
                     ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    ImGui_BeginChild("settings pane", (ImVec2) {0, -40}, 0, 0);

    switch (curPane) {
        case PANE_SYSTEM: {
            ImGui_SeparatorText("System");
            ImGui_InputText("Username", ctremu.username, sizeof ctremu.username,
                            0);
            static const char* languages[] = {
                "Japanese", "English",    "French",  "German",
                "Italian",  "Spanish",    "Chinese", "Korean",
                "Dutch",    "Portuguese", "Russian", "Taiwanese",
            };
            ImGui_ComboChar("System Language", &ctremu.language, languages,
                            countof(languages));

            ImGui_Checkbox("Auto Detect Region", &ctremu.detectRegion);
            ImGui_BeginDisabled(ctremu.detectRegion);
            static const char* regions[] = {
                "JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN",
            };
            ImGui_ComboChar("System Region", &ctremu.region, regions,
                            countof(regions));
            ImGui_EndDisabled();

            ImGui_SeparatorText("Camera");
            ImGui_Checkbox("Enable Camera", &ctremu.camEnable);

            break;
        }
        case PANE_CPU: {
            ImGui_SeparatorText("CPU JIT");
            ImGui_BeginDisabled(ctremu.initialized);
            ImGui_Checkbox("Use IR Interpreter", &g_jit_config.ir_interpret);
            ImGui_SetNextItemWidth(200);
            ImGui_InputInt("Maximum Block Instructions",
                           &g_jit_config.max_block_instrs);
            ImGui_Checkbox("Enable Optimization", &g_jit_config.optimize);
            ImGui_Checkbox("Enable Block Linking", &g_jit_config.linking);
            ImGui_EndDisabled();
            ImGui_SeparatorText("Memory");
            ImGui_Checkbox("Ignore Invalid Access", &ctremu.ignore_null);
            break;
        }
        case PANE_VIDEO: {
            ImGui_SeparatorText("Video");
            if (ImGui_Checkbox("VSync", &ctremu.vsync)) {
                if (ctremu.vsync) {
                    if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);
                } else {
                    SDL_GL_SetSwapInterval(0);
                }
            }
            static const char* layouts[] = {"Vertical", "Horizontal",
                                            "Large Screen"};
            ImGui_SetNextItemWidth(150);
            ImGui_ComboChar("Screen Layout", &ctremu.viewlayout, layouts,
                            countof(layouts));
            ImGui_Checkbox("Swap Screens", &ctremu.swapscreens);
            ImGui_SetNextItemWidth(150);
            ImGui_InputFloat("Large Screen Ratio", &ctremu.largescreenratio);

            ImGui_BeginDisabled(ctremu.initialized);
            static const char* filters[] = {"Nearest", "Bilinear",
                                            "Sharp Bilinear"};
            ImGui_SetNextItemWidth(150);
            ImGui_ComboChar("Postprocessing Filter", &ctremu.outputfilter,
                            filters, countof(filters));
            ImGui_EndDisabled();

            ImGui_SeparatorText("GPU");
            ImGui_BeginDisabled(ctremu.initialized);
            ImGui_SetNextItemWidth(150);
            ImGui_InputInt("Video Scale", &ctremu.videoscale);
            if (ctremu.videoscale < 1) ctremu.videoscale = 1;
            ImGui_SetNextItemWidth(150);
            ImGui_InputInt("Software Vertex Shader Threads",
                           &ctremu.vshthreads);
            if (ctremu.vshthreads < 0) ctremu.vshthreads = 0;
            if (ctremu.vshthreads > MAX_VSH_THREADS)
                ctremu.vshthreads = MAX_VSH_THREADS;
            ImGui_EndDisabled();
            ImGui_Checkbox("Shader JIT", &ctremu.shaderjit);
            ImGui_Checkbox("Hardware Vertex Shaders", &ctremu.hwvshaders);
            ImGui_Indent();
            ImGui_BeginDisabled(!ctremu.hwvshaders);
            ImGui_Checkbox("Safe Multiplication", &ctremu.safeShaderMul);
            ImGui_EndDisabled();
            ImGui_Unindent();
            // ImGui_Checkbox("Use Ubershader", &ctremu.ubershader);
            ImGui_Checkbox("Hash Textures", &ctremu.hashTextures);
            ImGui_Checkbox("Enable Texture Reinterpret",
                           &ctremu.reinterpretTexture);
            break;
        }
        case PANE_AUDIO: {
            ImGui_SeparatorText("Audio");
            ImGui_Checkbox("Audio Sync", &ctremu.audiosync);
            ImGui_SliderFloat("Volume", &ctremu.volume, 0, 200);
            static const char* audiomodes[] = {
                "Mono",
                "Stereo",
                "Surround",
            };
            ImGui_ComboChar("Audio Output Mode", &ctremu.audiomode, audiomodes,
                            countof(audiomodes));
            ImGui_SeparatorText("Microphone");
            ImGui_Checkbox("Enable Microphone", &ctremu.micEnable);
            break;
        }
        case PANE_INPUT: {
            if (ImGui_BeginTabBar("input tabs", 0)) {
                if (ImGui_BeginTabItem("Keyboard Input", nullptr, 0)) {
                    ImGui_BeginChild("keyboard input panel", (ImVec2) {}, 0, 0);
                    ImGui_BeginTable("input config", 2,
                                     ImGuiTableFlags_BordersOuterV);
                    config_input("A", &ctremu.inputmap.kb.a);
                    config_input("B", &ctremu.inputmap.kb.b);
                    config_input("X", &ctremu.inputmap.kb.x);
                    config_input("Y", &ctremu.inputmap.kb.y);
                    config_input("L", &ctremu.inputmap.kb.l);
                    config_input("R", &ctremu.inputmap.kb.r);
                    config_input("Start", &ctremu.inputmap.kb.start);
                    config_input("Select", &ctremu.inputmap.kb.select);
                    ImGui_TableNextRow();
                    config_input("D-Pad Left", &ctremu.inputmap.kb.dl);
                    config_input("D-Pad Right", &ctremu.inputmap.kb.dr);
                    config_input("D-Pad Up", &ctremu.inputmap.kb.du);
                    config_input("D-Pad Down", &ctremu.inputmap.kb.dd);
                    ImGui_TableNextRow();
                    config_input("Circle Pad Left", &ctremu.inputmap.kb.cl);
                    config_input("Circle Pad Right", &ctremu.inputmap.kb.cr);
                    config_input("Circle Pad Up", &ctremu.inputmap.kb.cu);
                    config_input("Circle Pad Down", &ctremu.inputmap.kb.cd);
                    config_input("Circle Pad Modifier",
                                 &ctremu.inputmap.kb.cmod);
                    ImGui_EndTable();
                    ImGui_SetNextItemWidth(200);
                    ImGui_SliderFloat("Circle Pad Modifier Scale",
                                      &ctremu.inputmap.kb.cmodscale, 0, 1);
                    ImGui_EndChild();
                    ImGui_EndTabItem();
                }

                if (ImGui_BeginTabItem("Freecam", nullptr,
                                       ImGuiTabItemFlags_None)) {
                    ImGui_BeginChild("freecam input panel", (ImVec2) {}, 0, 0);
                    ImGui_BeginTable("freecam config", 2,
                                     ImGuiTableFlags_BordersOuterV);
                    config_input("Move Forward", &ctremu.inputmap.freecam.mf);
                    config_input("Move Backward", &ctremu.inputmap.freecam.mb);
                    config_input("Move Left", &ctremu.inputmap.freecam.ml);
                    config_input("Move Right", &ctremu.inputmap.freecam.mr);
                    config_input("Move Up", &ctremu.inputmap.freecam.mu);
                    config_input("Move Down", &ctremu.inputmap.freecam.md);
                    ImGui_TableNextRow();
                    config_input("Look Up", &ctremu.inputmap.freecam.lu);
                    config_input("Look Down", &ctremu.inputmap.freecam.ld);
                    config_input("Look Left", &ctremu.inputmap.freecam.ll);
                    config_input("Look Right", &ctremu.inputmap.freecam.lr);
                    config_input("Roll Left", &ctremu.inputmap.freecam.rl);
                    config_input("Roll Right", &ctremu.inputmap.freecam.rr);
                    ImGui_TableNextRow();
                    config_input("Slow Modifier",
                                 &ctremu.inputmap.freecam.slow_mod);
                    config_input("Fast Modifier",
                                 &ctremu.inputmap.freecam.fast_mod);
                    ImGui_EndTable();
                    ImGui_EndChild();
                    ImGui_EndTabItem();
                }

                ImGui_EndTabBar();
            }
            break;
        }
        case PANE_MAX:
            break;
    }
    ImGui_EndChild();

    ImGui_Separator();

    ImGui_BeginDisabled(ctremu.initialized);
    if (ImGui_Button("Reset All")) {
        emulator_load_default_settings();
    }
    ImGui_EndDisabled();

    ImGui_SameLine();

    if (ImGui_Button("Close")) {
        uistate.settings = false;
    }

    ImGui_EndChild();

    ImGui_End();
}

void draw_textureview() {
    if (!ctremu.initialized) uistate.textureview = false;
    if (!uistate.textureview) return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui_GetIO()->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    ImGui_SetNextWindowClass(&(ImGuiWindowClass) {
        .ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge});

    ImGui_SetNextWindowSize((ImVec2) {800, 700}, ImGuiCond_FirstUseEver);

    ImGui_Begin("GPU Texture Viewer", &uistate.textureview, flags);

    ImGui_BeginTabBar("##texturetabbar", 0);

    if (ImGui_BeginTabItem("Textures", nullptr, 0)) {

        static int curTex = 0;

        auto texcache = &ctremu.system.gpu.textures;

        ImGui_PushStyleColorImVec4(ImGuiCol_ChildBg,
                                   *ImGui_GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_SelectableTextAlign,
                                 (ImVec2) {0.5, 0.5});
        ImGui_BeginChild("##list", (ImVec2) {200, 0}, 0, 0);

        for (int i = 0; i < TEX_MAX; i++) {
            char buf[100];

            if (texcache->d[i].key) {
                sprintf(buf, "Texture %d", i);
                ImGui_BeginDisabled(false);
            } else {
                sprintf(buf, "[Empty]##%d", i);
                ImGui_BeginDisabled(true);
            }
            if (ImGui_SelectableEx(buf, curTex == i, 0, (ImVec2) {})) {
                curTex = i;
            }
            ImGui_EndDisabled();
        }

        ImGui_EndChild();
        ImGui_PopStyleVar();
        ImGui_PopStyleColor();

        ImGui_SameLine();

        ImGui_BeginChild("##texture pane", (ImVec2) {},
                         ImGuiChildFlags_AlwaysUseWindowPadding, 0);

        auto tex = &texcache->d[curTex];

        if (tex->key) {
            float w, h;
            if (tex->width > tex->height) {
                w = 512;
                h = (float) tex->height * 512 / tex->width;
            } else {
                h = 512;
                w = (float) tex->width * 512 / tex->height;
            }

            ImGui_ImageWithBgEx((ImTextureRef) {0, tex->tex}, (ImVec2) {w, h},
                                (ImVec2) {0, 1}, (ImVec2) {1, 0},
                                (ImVec4) {0.25, 0.25, 0.25, 1},
                                (ImVec4) {1, 1, 1, 1});
            static const char* fmts[] = {
                "RGBA8888", "RGB888", "RGBA5551", "RGB565", "RGBA4444", "LA88",
                "RG88",     "L8",     "A8",       "IA44",   "I4",       "A4",
                "ETC1",     "ETC1A4", "???",      "???"};
            ImGui_Text("Addr: %#lx  Size: %dx%d  Format: %s", tex->paddr,
                       tex->width, tex->height, fmts[tex->fmt]);
            ImGui_Text("Hash: %016lx", tex->hash);
        }
        ImGui_EndChild();

        ImGui_EndTabItem();
    }

    if (ImGui_BeginTabItem("Framebuffers", nullptr, 0)) {

        static int curFb = 0;

        auto fbcache = &ctremu.system.gpu.fbs;

        ImGui_PushStyleColorImVec4(ImGuiCol_ChildBg,
                                   *ImGui_GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_SelectableTextAlign,
                                 (ImVec2) {0.5, 0.5});
        ImGui_BeginChild("##list", (ImVec2) {200, 0}, 0, 0);

        for (int i = 0; i < FB_MAX; i++) {
            char buf[100];

            if (fbcache->d[i].key) {
                sprintf(buf, "Framebuffer %d", i);
                ImGui_BeginDisabled(false);
            } else {
                sprintf(buf, "[Empty]##%d", i);
                ImGui_BeginDisabled(true);
            }
            if (ImGui_SelectableEx(buf, curFb == i, 0, (ImVec2) {})) {
                curFb = i;
            }
            ImGui_EndDisabled();
        }

        ImGui_EndChild();
        ImGui_PopStyleVar();
        ImGui_PopStyleColor();

        ImGui_SameLine();

        ImGui_BeginChild("##fb pane", (ImVec2) {},
                         ImGuiChildFlags_AlwaysUseWindowPadding, 0);

        auto fb = &fbcache->d[curFb];

        if (fb->key) {

            float w, h;
            if (fb->width > fb->height) {
                w = 512;
                h = (float) fb->height * 512 / fb->width;
            } else {
                h = 512;
                w = (float) fb->width * 512 / fb->height;
            }

            static int bufselect;
            ImGui_RadioButtonIntPtr("Color Buffer", &bufselect, 0);
            ImGui_SameLine();
            ImGui_RadioButtonIntPtr("Depth Buffer", &bufselect, 1);

            if (bufselect == 0) {
                ImGui_ImageWithBgEx(
                    (ImTextureRef) {0, fb->color_tex}, (ImVec2) {w, h},
                    (ImVec2) {0, 1}, (ImVec2) {1, 0},
                    (ImVec4) {0.25, 0.25, 0.25, 1}, (ImVec4) {1, 1, 1, 1});
            } else {
                ImGui_ImageWithBgEx(
                    (ImTextureRef) {0, fb->depth_tex}, (ImVec2) {w, h},
                    (ImVec2) {0, 1}, (ImVec2) {1, 0},
                    (ImVec4) {0.25, 0.25, 0.25, 1}, (ImVec4) {1, 1, 1, 1});
            }
            static const char* fmts[] = {"RGBA8888", "RGB888",   "RGB565",
                                         "RGBA5551", "RGBA4444", "???",
                                         "???",      "???"};
            ImGui_Text("Color Addr: %#lx  Size: %dx%d  Format: %s",
                       fb->color_paddr, fb->width, fb->height,
                       fmts[fb->color_fmt]);
            ImGui_Text("Depth Addr: %#x", fb->depth_paddr);
        }
        ImGui_EndChild();

        ImGui_EndTabItem();
    }
    ImGui_EndTabBar();

    ImGui_End();
}

int samplenum = 2000;
float samplerange = 0.25f;

void plot_samples(DSPSampHist* wave) {
    float samples[FIFO_MAX(wave[0])];
    for (int i = 0; i < 2; i++) {
        ImGui_PushIDInt(i);
        ImGui_TableNextColumn();
        FIFO_foreach_ring(it, wave[i]) {
            samples[it.i] = (float) *it.p / BIT(16);
        }
        ImGui_PlotLinesEx("##", samples + countof(samples) - samplenum,
                          samplenum, 0, nullptr, -samplerange, samplerange,
                          (ImVec2) {200, 50}, 4);
        ImGui_PopID();
    }
}

void draw_audioview() {
    if (!ctremu.initialized) uistate.audioview = false;
    if (!uistate.audioview) return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui_GetIO()->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    ImGui_SetNextWindowClass(&(ImGuiWindowClass) {
        .ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge});

    ImGui_SetNextWindowSize((ImVec2) {650, 400}, ImGuiCond_FirstUseEver);

    ImGui_Begin("DSP Audio Channels", &uistate.audioview, flags);

    ImGui_SliderInt("Sample Length", &samplenum, 0,
                    FIFO_MAX(g_dsp_chn_hist[0][0]));

    ImGui_SliderFloat("Amplitude Range", &samplerange, 0, 1);

    ImGui_BeginChild("audioviewchild", (ImVec2) {0, -40}, 0, 0);

    ImGui_BeginTable("##audioview", 4,
                     ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                         ImGuiTableFlags_BordersOuterV |
                         ImGuiTableFlags_ScrollY);

    ImGui_TableSetupScrollFreeze(0, 1);
    ImGui_TableSetupColumn("##", 0);
    ImGui_TableSetupColumn("Left", 0);
    ImGui_TableSetupColumn("Right", 0);
    ImGui_TableSetupColumn("Disabled", 0);
    ImGui_TableHeadersRow();

    ImGui_TableNextRow();
    ImGui_TableNextColumn();
    ImGui_Text("Final Output");
    ImGui_PushIDInt(-1);
    plot_samples(g_dsp_hist);
    ImGui_PopID();
    ImGui_TableNextColumn();

    ImGui_TableNextRow();

    for (int i = 0; i < DSP_CHANNELS; i++) {
        ImGui_PushIDInt(i);
        ImGui_TableNextRow();
        ImGui_TableNextColumn();
        ImGui_Text("Channel %d", i);
        plot_samples(g_dsp_chn_hist[i]);
        ImGui_TableNextColumn();
        bool dis = g_dsp_chn_disable & BIT(i);
        g_dsp_chn_disable &= ~BIT(i);
        ImGui_Checkbox("##", &dis);
        g_dsp_chn_disable |= dis << i;
        ImGui_PopID();
    }

    ImGui_EndTable();

    ImGui_EndChild();

    ImGui_Separator();

    if (ImGui_Button("Enable All")) {
        g_dsp_chn_disable = 0;
    }
    ImGui_SameLine();
    if (ImGui_Button("Disable All")) {
        g_dsp_chn_disable = ~0;
    }
    ImGui_SameLine();
    if (ImGui_Button("Close")) {
        uistate.audioview = false;
    }

    ImGui_End();
}

void draw_gui() {
    draw_swkbd();
    draw_settings();
    draw_textureview();
    draw_audioview();
}
