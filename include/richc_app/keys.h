/*
 * keys.h - keyboard key codes, mouse buttons, and modifier flags.
 *
 * Values are chosen to match GLFW's constants directly so the GLFW backend
 * can cast without a lookup table.  An alternative backend must map its own
 * codes to these values before dispatching events.
 */

#ifndef RC_APP_KEYS_H_
#define RC_APP_KEYS_H_

#include <stdint.h>

/* ---- action ---- */

typedef enum {
    RC_ACTION_RELEASE = 0,
    RC_ACTION_PRESS   = 1,
    RC_ACTION_REPEAT  = 2
} rc_action;

/* ---- modifier flags ---- */

typedef enum {
    RC_MOD_SHIFT    = 0x0001,
    RC_MOD_CTRL     = 0x0002,
    RC_MOD_ALT      = 0x0004,
    RC_MOD_SUPER    = 0x0008,
    RC_MOD_CAPS     = 0x0010,
    RC_MOD_NUMLOCK  = 0x0020
} rc_mod;

/* ---- mouse buttons ---- */

typedef enum {
    RC_MOUSE_BUTTON_LEFT   = 0,
    RC_MOUSE_BUTTON_RIGHT  = 1,
    RC_MOUSE_BUTTON_MIDDLE = 2
} rc_mouse_button;

/* ---- key codes (printable) ---- */

typedef enum {
    RC_KEY_SPACE         = 32,
    RC_KEY_APOSTROPHE    = 39,
    RC_KEY_COMMA         = 44,
    RC_KEY_MINUS         = 45,
    RC_KEY_PERIOD        = 46,
    RC_KEY_SLASH         = 47,
    RC_KEY_0             = 48,
    RC_KEY_1             = 49,
    RC_KEY_2             = 50,
    RC_KEY_3             = 51,
    RC_KEY_4             = 52,
    RC_KEY_5             = 53,
    RC_KEY_6             = 54,
    RC_KEY_7             = 55,
    RC_KEY_8             = 56,
    RC_KEY_9             = 57,
    RC_KEY_SEMICOLON     = 59,
    RC_KEY_EQUAL         = 61,
    RC_KEY_A             = 65,
    RC_KEY_B             = 66,
    RC_KEY_C             = 67,
    RC_KEY_D             = 68,
    RC_KEY_E             = 69,
    RC_KEY_F             = 70,
    RC_KEY_G             = 71,
    RC_KEY_H             = 72,
    RC_KEY_I             = 73,
    RC_KEY_J             = 74,
    RC_KEY_K             = 75,
    RC_KEY_L             = 76,
    RC_KEY_M             = 77,
    RC_KEY_N             = 78,
    RC_KEY_O             = 79,
    RC_KEY_P             = 80,
    RC_KEY_Q             = 81,
    RC_KEY_R             = 82,
    RC_KEY_S             = 83,
    RC_KEY_T             = 84,
    RC_KEY_U             = 85,
    RC_KEY_V             = 86,
    RC_KEY_W             = 87,
    RC_KEY_X             = 88,
    RC_KEY_Y             = 89,
    RC_KEY_Z             = 90,
    RC_KEY_LEFT_BRACKET  = 91,
    RC_KEY_BACKSLASH     = 92,
    RC_KEY_RIGHT_BRACKET = 93,
    RC_KEY_GRAVE         = 96,

    /* ---- function / navigation keys ---- */

    RC_KEY_ESCAPE        = 256,
    RC_KEY_ENTER         = 257,
    RC_KEY_TAB           = 258,
    RC_KEY_BACKSPACE     = 259,
    RC_KEY_INSERT        = 260,
    RC_KEY_DELETE        = 261,
    RC_KEY_RIGHT         = 262,
    RC_KEY_LEFT          = 263,
    RC_KEY_DOWN          = 264,
    RC_KEY_UP            = 265,
    RC_KEY_PAGE_UP       = 266,
    RC_KEY_PAGE_DOWN     = 267,
    RC_KEY_HOME          = 268,
    RC_KEY_END           = 269,
    RC_KEY_CAPS_LOCK     = 280,
    RC_KEY_SCROLL_LOCK   = 281,
    RC_KEY_NUM_LOCK      = 282,
    RC_KEY_PRINT_SCREEN  = 283,
    RC_KEY_PAUSE         = 284,
    RC_KEY_F1            = 290,
    RC_KEY_F2            = 291,
    RC_KEY_F3            = 292,
    RC_KEY_F4            = 293,
    RC_KEY_F5            = 294,
    RC_KEY_F6            = 295,
    RC_KEY_F7            = 296,
    RC_KEY_F8            = 297,
    RC_KEY_F9            = 298,
    RC_KEY_F10           = 299,
    RC_KEY_F11           = 300,
    RC_KEY_F12           = 301,
    RC_KEY_LEFT_SHIFT    = 340,
    RC_KEY_LEFT_CTRL     = 341,
    RC_KEY_LEFT_ALT      = 342,
    RC_KEY_LEFT_SUPER    = 343,
    RC_KEY_RIGHT_SHIFT   = 344,
    RC_KEY_RIGHT_CTRL    = 345,
    RC_KEY_RIGHT_ALT     = 346,
    RC_KEY_RIGHT_SUPER   = 347,
    RC_KEY_MENU          = 348,

    RC_KEY_UNKNOWN       = -1
} rc_key;

#endif /* RC_APP_KEYS_H_ */
