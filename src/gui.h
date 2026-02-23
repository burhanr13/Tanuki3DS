#ifndef GUI_H
#define GUI_H

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_USE_SDL3
#define CIMGUI_USE_OPENGL3
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

#define igGetIO igGetIO_Nil
#define igMenuItem igMenuItem_Bool
#define igMenuItemP igMenuItem_BoolPtr
#define igSelectable igSelectable_Bool
#define igSelectableP igSelectable_BoolPtr
#define igCombo igCombo_Str_arr
#define igListBox igListBox_Str_arr
#define igBeginChild igBeginChild_Str

extern struct UIState {
    bool menubar;
    bool settings;

    bool textureview;
    bool audioview;

    int* waiting_key;
} uistate;

void setup_gui_theme();
void draw_gui();

#endif
