#include "gui.h"

#include <SDL3/SDL.h>
#include <dirent.h>
#include <unistd.h>

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

    igStyleColorsDark(nullptr);
}

void draw_menubar() {
    if (!uistate.menubar) return;

    if (igBeginMainMenuBar()) {
        if (igBeginMenu("File", true)) {
            if (igMenuItem("Open", "F2", false, true)) {
                load_rom_dialog();
            }
            if (igBeginMenu("Open Recent", ctremu.history[0])) {
                for (int i = 0; i < HISTORYLEN; i++) {
                    if (!ctremu.history[i]) break;
                    if (igMenuItem(ctremu.history[i], nullptr, false, true)) {
                        emulator_set_rom(ctremu.history[i]);
                    }
                }
                igEndMenu();
            }
            igSeparator();

            if (igBeginMenu("Load System File", true)) {
                if (igMenuItem("Shared Font", nullptr, false, true)) {
                    load_sysfile_dialog("font.bcfnt");
                }
                if (igMenuItem("Mii Data", nullptr, false, true)) {
                    load_sysfile_dialog("mii.app.romfs");
                }
                igEndMenu();
            }

            if (igMenuItem("Open Tanuki3DS Folder", nullptr, false, true)) {
                char* cwd = getcwd(nullptr, 0);
                char* cmd;
                asprintf(&cmd, OPEN_CMD " '%s'", cwd);
                system(cmd);
                free(cwd);
                free(cmd);
            }

            igSeparator();
            if (igMenuItem("Exit", nullptr, false, true)) {
                ctremu.running = false;
            }
            igEndMenu();
        }
        if (igBeginMenu("Emulation", ctremu.initialized)) {
            if (igMenuItem("Reset", "F1", false, true)) {
                ctremu.pending_reset = true;
            }
            igMenuItemP("Pause", "F5", &ctremu.pause, true);
            if (igMenuItem("Stop", nullptr, false, true)) {
                emulator_set_rom(nullptr);
            }
            igSeparator();

            igMenuItemP("Fast Forward", "Tab", &ctremu.fastforward, true);
            igMenuItemP("Mute", "F6", &ctremu.mute, true);

            igSeparator();

            if (igMenuItemP("Free Camera", "F7", &ctremu.freecam_enable,
                            true)) {
                glm_mat4_identity(ctremu.freecam_mtx);
                renderer_gl_update_freecam(&ctremu.system.gpu.gl);
            }

            igEndMenu();
        }

        if (igBeginMenu("View", true)) {
            if (igMenuItemP("Fullscreen", "F11", &ctremu.fullscreen, true)) {
                SDL_SetWindowFullscreen(g_window, ctremu.fullscreen);
            }
            if (igBeginMenu("Screen Layout", true)) {
                if (igMenuItem("Vertical", nullptr,
                               ctremu.viewlayout == LAYOUT_DEFAULT, true)) {
                    ctremu.viewlayout = LAYOUT_DEFAULT;
                }
                if (igMenuItem("Horizontal", nullptr,
                               ctremu.viewlayout == LAYOUT_HORIZONTAL, true)) {
                    ctremu.viewlayout = LAYOUT_HORIZONTAL;
                }
                if (igMenuItem("Large Screen", nullptr,
                               ctremu.viewlayout == LAYOUT_LARGETOP, true)) {
                    ctremu.viewlayout = LAYOUT_LARGETOP;
                }
                igSeparator();
                igMenuItemP("Swap Screens", "F9", &ctremu.swapscreens, true);
                igEndMenu();
            }
            igSeparator();

            if (igMenuItem("Settings", "F3", false, true)) {
                uistate.settings = true;
            }

            igSeparator();

            if (igMenuItem("Texture Viewer", nullptr, false,
                           ctremu.initialized)) {
                uistate.textureview = true;
            }

            if (igMenuItem("Audio Channels", nullptr, false,
                           ctremu.initialized)) {
                uistate.audioview = true;
            }

            igEndMenu();
        }

        if (igBeginMenu("Debug", ctremu.initialized)) {
            igMenuItemP("Verbose Log", nullptr, &g_infologs, true);
            igMenuItemP("CPU Trace Log", nullptr, &g_cpulog, true);
            igSeparator();
            igMenuItemP("Wireframe", nullptr, &g_wireframe, true);
            igEndMenu();
        }

        if (igBeginMenu("About", true)) {
            igTextLinkOpenURL("GitHub",
                              "https://github.com/burhanr13/Tanuki3DS");
            igTextLinkOpenURL("Discord", "https://discord.gg/6ya65fvD3g");
            igEndMenu();
        }

        igSpacing();
        igTextDisabled("Esc to toggle menu bar");

        igEndMainMenuBar();
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

    igSetNextWindowViewport(igGetMainViewport()->ID);
    igSetNextWindowPos(igGetMainViewport()->WorkPos, 0, (ImVec2) {});
    igSetNextWindowSize(igGetMainViewport()->WorkSize, 0);
    igBegin("game list", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

    if (igButton("Select Game Folder...", (ImVec2) {})) {
        SDL_ShowOpenFolderDialog(game_dir_callback, nullptr, g_window, nullptr,
                                 false);
    }
    igSameLine(0, -1);
    if (ctremu.gamedir) {
        igText("Current: %s", ctremu.gamedir);
    } else {
        igText("Current: [None]");
    }
    igSameLine(0, -1);
    if (igButton("Refresh", (ImVec2) {})) {
        gamelist_refresh = true;
    }

    igBeginChild("gamelist_child", (ImVec2) {}, 0, 0);
    igBeginTable("gamelist", 5,
                 ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                     ImGuiTableFlags_ScrollY,
                 (ImVec2) {}, 0);

    igTableSetupScrollFreeze(0, 1);
    igTableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 50, 0);
    igTableSetupColumn("Title", 0, 0, 0);
    igTableSetupColumn("Publisher", 0, 0, 0);
    igTableSetupColumn("Region", 0, 0, 0);
    igTableSetupColumn("Size", 0, 0, 0);
    igTableHeadersRow();

    Vec_foreach(g, gamelist) {
        igPushID_Str(g->filename);
        igTableNextRow(0, 0);
        igTableNextColumn();
        if (igSelectable("##", false,
                         ImGuiSelectableFlags_SpanAllColumns |
                             ImGuiSelectableFlags_AllowOverlap |
                             ImGuiSelectableFlags_AllowDoubleClick,
                         (ImVec2) {0, 48})) {
            if (igIsMouseDoubleClicked_Nil(0)) emulator_set_rom(g->filename);
        }
        igSameLine(0, -1);
        igImage((ImTextureRef) {0, g->icontex}, (ImVec2) {48, 48},
                (ImVec2) {0, 0}, (ImVec2) {1, 1});
        igTableNextColumn();
        igText("%s", g->gamename);
        igTextDisabled("%s", strrchr(g->filename, '/') + 1);
        igTableNextColumn();
        igText("%s", g->publisher);
        igTableNextColumn();
        static const char* regions[] = {
            "JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN",
        };
        if ((g->region & 0x7f) == 0x7f) {
            igText("Any");
        } else {
            igText("%s", regions[__builtin_ctz(g->region)]);
        }
        igTableNextColumn();
        if (g->size < BIT(20)) {
            igText("%.1f KiB", (double) g->size / BIT(10));
        } else if (g->size < BIT(30)) {
            igText("%.1f MiB", (double) g->size / BIT(20));
        } else {
            igText("%.1f GiB", (double) g->size / BIT(30));
        }
        igPopID();
    }

    igEndTable();
    igEndChild();

    igEnd();
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
        PANE_CPU,
        PANE_VIDEO,
        PANE_AUDIO,
        PANE_INPUT,
        PANE_MAX
    } curPane = PANE_SYSTEM;

    static const char* pane_names[] = {"System", "CPU", "Video", "Audio",
                                       "Input"};

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
    igPushStyleVar_Vec2(ImGuiStyleVar_SelectableTextAlign, (ImVec2) {0.5, 0.5});
    igBeginChild("sidebar", (ImVec2) {100, 0}, 0, 0);
    igSeparator();
    for (int i = 0; i < PANE_MAX; i++) {
        if (igSelectable(pane_names[i], curPane == i, 0, (ImVec2) {})) {
            curPane = i;
        }
        igSeparator();
    }
    igEndChild();
    igPopStyleVar(1);
    igPopStyleColor(1);

    igSameLine(0, -1);

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

            igCheckbox("Auto Detect Region", &ctremu.detectRegion);
            igBeginDisabled(ctremu.detectRegion);
            static const char* regions[] = {
                "JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN",
            };
            igCombo("System Region", &ctremu.region, regions, countof(regions),
                    0);
            igEndDisabled();

            igSeparatorText("Camera");
            igCheckbox("Enable Camera", &ctremu.camEnable);

            break;
        }
        case PANE_CPU: {
            igSeparatorText("CPU JIT");
            igBeginDisabled(ctremu.initialized);
            igCheckbox("Use IR Interpreter", &g_jit_config.ir_interpret);
            igSetNextItemWidth(200);
            igInputInt("Maximum Block Instructions",
                       &g_jit_config.max_block_instrs, 1, 1, 0);
            igCheckbox("Enable Optimization", &g_jit_config.optimize);
            igCheckbox("Enable Block Linking", &g_jit_config.linking);
            igEndDisabled();
            igSeparatorText("Memory");
            igCheckbox("Ignore Invalid Access", &ctremu.ignore_null);
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
            static const char* layouts[] = {"Vertical", "Horizontal",
                                            "Large Screen"};
            igSetNextItemWidth(150);
            igCombo("Screen Layout", &ctremu.viewlayout, layouts,
                    countof(layouts), 0);
            igCheckbox("Swap Screens", &ctremu.swapscreens);
            igSetNextItemWidth(150);
            igInputFloat("Large Screen Ratio", &ctremu.largescreenratio, 1, 1,
                         nullptr, 0);

            igBeginDisabled(ctremu.initialized);
            static const char* filters[] = {"Nearest", "Bilinear",
                                            "Sharp Bilinear"};
            igSetNextItemWidth(150);
            igCombo("Postprocessing Filter", &ctremu.outputfilter, filters,
                    countof(filters), 0);
            igEndDisabled();

            igSeparatorText("GPU");
            igBeginDisabled(ctremu.initialized);
            igSetNextItemWidth(150);
            igInputInt("Video Scale", &ctremu.videoscale, 1, 1, 0);
            if (ctremu.videoscale < 1) ctremu.videoscale = 1;
            igSetNextItemWidth(150);
            igInputInt("Software Vertex Shader Threads", &ctremu.vshthreads, 1,
                       1, 0);
            if (ctremu.vshthreads < 0) ctremu.vshthreads = 0;
            if (ctremu.vshthreads > MAX_VSH_THREADS)
                ctremu.vshthreads = MAX_VSH_THREADS;
            igEndDisabled();
            igCheckbox("Shader JIT", &ctremu.shaderjit);
            igCheckbox("Hardware Vertex Shaders", &ctremu.hwvshaders);
            igIndent(0);
            igBeginDisabled(!ctremu.hwvshaders);
            igCheckbox("Safe Multiplication", &ctremu.safeShaderMul);
            igEndDisabled();
            igUnindent(0);
            // igCheckbox("Use Ubershader", &ctremu.ubershader);
            igCheckbox("Hash Textures", &ctremu.hashTextures);
            break;
        }
        case PANE_AUDIO: {
            igSeparatorText("Audio");
            igCheckbox("Audio Sync", &ctremu.audiosync);
            igSliderFloat("Volume", &ctremu.volume, 0, 200, nullptr, 0);
            static const char* audiomodes[] = {
                "Mono",
                "Stereo",
                "Surround",
            };
            igCombo("Audio Output Mode", &ctremu.audiomode, audiomodes,
                    countof(audiomodes), 0);
            igSeparatorText("Microphone");
            igCheckbox("Enable Microphone", &ctremu.micEnable);
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

    igSameLine(0, -1);

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
        igPushStyleVar_Vec2(ImGuiStyleVar_SelectableTextAlign,
                            (ImVec2) {0.5, 0.5});
        igBeginChild("##list", (ImVec2) {200, 0}, 0, 0);

        for (int i = 0; i < TEX_MAX; i++) {
            char buf[100];

            if (texcache->d[i].key) {
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
        igPopStyleVar(1);
        igPopStyleColor(1);

        igSameLine(0, -1);

        igBeginChild("##texture pane", (ImVec2) {},
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
        igPushStyleVar_Vec2(ImGuiStyleVar_SelectableTextAlign,
                            (ImVec2) {0.5, 0.5});
        igBeginChild("##list", (ImVec2) {200, 0}, 0, 0);

        for (int i = 0; i < FB_MAX; i++) {
            char buf[100];

            if (fbcache->d[i].key) {
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
        igPopStyleVar(1);
        igPopStyleColor(1);

        igSameLine(0, -1);

        igBeginChild("##fb pane", (ImVec2) {},
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
            igRadioButton_IntPtr("Color Buffer", &bufselect, 0);
            igSameLine(0, -1);
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
                     ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_ScrollY,
                 (ImVec2) {}, 0);

    igTableSetupScrollFreeze(0, 1);
    igTableSetupColumn("##", 0, 0, 0);
    igTableSetupColumn("Left", 0, 0, 0);
    igTableSetupColumn("Right", 0, 0, 0);
    igTableSetupColumn("Disabled", 0, 0, 0);
    igTableHeadersRow();

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
    igSameLine(0, -1);
    if (igButton("Disable All", (ImVec2) {})) {
        g_dsp_chn_disable = ~0;
    }
    igSameLine(0, -1);
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
