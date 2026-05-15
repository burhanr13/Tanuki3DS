#ifndef GUI_H
#define GUI_H

extern struct UIState {
    bool menubar;
    bool settings;

    bool textureview;
    bool audioview;

    int* waiting_key;
} uistate;


void load_rom_dialog();
void setup_gui_theme();
void draw_menubar();
void draw_gamelist();
void draw_gui();

#endif
