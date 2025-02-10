#ifndef APPLETS_H
#define APPLETS_H

#include <3ds.h>
#include <common.h>

typedef struct _3DS E3DS;

// swkbd types copied from libctru

#define SWKBD_MAX_BUTTON_TEXT_LEN 16
#define SWKBD_MAX_HINT_TEXT_LEN 64
#define SWKBD_MAX_CALLBACK_MSG_LEN 256

enum {
    SWKBD_D0_CLICK,
    SWKBD_D1_CLICK0,
    SWKBD_D1_CLICK1,
    SWKBD_D2_CLICK0,
    SWKBD_D2_CLICK1,
    SWKBD_D2_CLICK2,
};

typedef struct {
    int type;
    int num_buttons_m1;
    int valid_input;
    int password_mode;
    int is_parental_screen;
    int darken_top_screen;
    u32 filter_flags;
    u32 save_state_flags;
    u16 max_text_len;
    u16 dict_word_count;
    u16 max_digits;
    u16 button_text[3][SWKBD_MAX_BUTTON_TEXT_LEN + 1];
    u16 numpad_keys[2];
    u16 hint_text[SWKBD_MAX_HINT_TEXT_LEN + 1];
    bool predictive_input;
    bool multiline;
    bool fixed_width;
    bool allow_home;
    bool allow_reset;
    bool allow_power;
    bool unknown;
    bool default_qwerty;
    bool button_submits_text[4];
    u16 language;
    int initial_text_offset;
    int dict_offset;
    int initial_status_offset;
    int initial_learning_offset;
    u32 shared_memory_size;
    u32 version;
    int result;
    int status_offset;
    int learning_offset;
    int text_offset;
    u16 text_length;
    int callback_result;
    u16 callback_msg[SWKBD_MAX_CALLBACK_MSG_LEN + 1];
    bool skip_at_check;
    u8 reserved[171];
} SwkbdState;

void swkbd_run(E3DS* s, u32 paramsvaddr, u32 shmemvaddr);

#endif
