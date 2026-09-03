#ifndef PS2_KBD_H
#define PS2_KBD_H

#include "common_headers/types.h"

#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KBD_COMMAND_PORT 0x64

#define KBD_MOD_LSHIFT 0x01
#define KBD_MOD_RSHIFT 0x02
#define KBD_MOD_LCTRL  0x04
#define KBD_MOD_RCTRL  0x08
#define KBD_MOD_LALT   0x10
#define KBD_MOD_RALT   0x20
#define KBD_MOD_LGUI   0x40
#define KBD_MOD_RGUI   0x80

#define KBD_MOD_SHIFT (KBD_MOD_LSHIFT | KBD_MOD_RSHIFT)
#define KBD_MOD_CTRL  (KBD_MOD_LCTRL  | KBD_MOD_RCTRL)
#define KBD_MOD_ALT   (KBD_MOD_LALT   | KBD_MOD_RALT)
#define KBD_MOD_GUI   (KBD_MOD_LGUI   | KBD_MOD_RGUI)

#define KBD_LOCK_SCROLL 0x01
#define KBD_LOCK_NUM    0x02
#define KBD_LOCK_CAPS   0x04

#define KEY_LCTRL      0x1D
#define KEY_LSHIFT     0x2A
#define KEY_RSHIFT     0x36
#define KEY_LALT       0x38
#define KEY_CAPSLOCK   0x3A
#define KEY_NUMLOCK    0x45
#define KEY_SCROLLLOCK 0x46

#define KEY_EXT_BASE   0x0100
#define KEY_KP_ENTER   (KEY_EXT_BASE + 0x1C)
#define KEY_RCTRL      (KEY_EXT_BASE + 0x1D)
#define KEY_KP_DIVIDE  (KEY_EXT_BASE + 0x35)
#define KEY_RALT       (KEY_EXT_BASE + 0x38)
#define KEY_HOME       (KEY_EXT_BASE + 0x47)
#define KEY_UP         (KEY_EXT_BASE + 0x48)
#define KEY_PGUP       (KEY_EXT_BASE + 0x49)
#define KEY_LEFT       (KEY_EXT_BASE + 0x4B)
#define KEY_RIGHT      (KEY_EXT_BASE + 0x4D)
#define KEY_END        (KEY_EXT_BASE + 0x4F)
#define KEY_DOWN       (KEY_EXT_BASE + 0x50)
#define KEY_PGDN       (KEY_EXT_BASE + 0x51)
#define KEY_INSERT     (KEY_EXT_BASE + 0x52)
#define KEY_DELETE     (KEY_EXT_BASE + 0x53)
#define KEY_LGUI       (KEY_EXT_BASE + 0x5B)
#define KEY_RGUI       (KEY_EXT_BASE + 0x5C)
#define KEY_MENU       (KEY_EXT_BASE + 0x5D)
#define KEY_PRTSCN     0x0200
#define KEY_PAUSE      0x0201

typedef struct {
    uint8_t scancode;
    uint16_t keycode;
    char ascii;
    uint8_t pressed;
    uint8_t modifiers;
} kbd_event_t;

void kbd_init(void);

int kbd_read_event(kbd_event_t* out);
int kbd_has_event(void);

uint8_t kbd_get_modifiers(void);
uint8_t kbd_get_locks(void);

#endif
