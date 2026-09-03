#include "ps2kbd.h"
#include "pic.h"
#include "common_headers/io.h"
#include "arch/x86/isr.h"
#include "misc/kbd_us_qwerty.h"

#define KBD_EVENT_QUEUE_SIZE 64

static kbd_event_t event_queue[KBD_EVENT_QUEUE_SIZE];
static uint8_t event_head = 0;
static uint8_t event_tail = 0;

static uint8_t modifiers = 0;
static uint8_t locks = 0;
static uint8_t extended = 0;
static uint8_t pause_bytes_left = 0;

static void kbd_wait_input_clear(void)
{
    while (inb(KBD_STATUS_PORT) & 0x02) { }
}

static void kbd_update_leds(void)
{
    uint8_t led_mask = 0;
    if (locks & KBD_LOCK_SCROLL) led_mask |= 0x01;
    if (locks & KBD_LOCK_NUM)    led_mask |= 0x02;
    if (locks & KBD_LOCK_CAPS)   led_mask |= 0x04;

    kbd_wait_input_clear();
    outb(KBD_DATA_PORT, 0xED);
    kbd_wait_input_clear();
    outb(KBD_DATA_PORT, led_mask);
}

static void set_mod(uint8_t bit, uint8_t pressed)
{
    if (pressed) {
        modifiers |= bit;
    } else {
        modifiers &= (uint8_t) ~bit;
    }
}

static char translate_ascii(uint8_t raw, uint8_t shift_active, uint8_t caps_active)
{
    if (raw >= 128) {
        return 0;
    }

    char lower = kbd_us_ascii_lower[raw];
    char upper = kbd_us_ascii_upper[raw];

    if (lower >= 'a' && lower <= 'z') {
        return shift_active ^ caps_active ? upper : lower;
    }

    return shift_active ? upper : lower;
}

static void push_event(uint8_t sc, uint16_t keycode, char ascii, uint8_t pressed)
{
    uint8_t next_head = (uint8_t) ((event_head + 1) % KBD_EVENT_QUEUE_SIZE);
    if (next_head == event_tail) {
        return;
    }

    event_queue[event_head].scancode = sc;
    event_queue[event_head].keycode = keycode;
    event_queue[event_head].ascii = ascii;
    event_queue[event_head].pressed = pressed;
    event_queue[event_head].modifiers = modifiers;
    event_head = next_head;
}

static void keyboard_irq_handler(registers_t* regs)
{
    (void) regs;
    uint8_t sc = inb(KBD_DATA_PORT);

    if (pause_bytes_left > 0) {
        pause_bytes_left--;
        if (pause_bytes_left == 0) {
            push_event(sc, KEY_PAUSE, 0, 1);
            push_event(sc, KEY_PAUSE, 0, 0);
        }
        return;
    }

    if (sc == 0xE1) {
        pause_bytes_left = 5;
        return;
    }

    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    uint8_t pressed = (uint8_t) !(sc & 0x80);
    uint8_t raw = (uint8_t) (sc & 0x7F);
    uint8_t was_extended = extended;
    extended = 0;

    uint16_t keycode = raw;
    char ascii = 0;

    if (was_extended) {
        if (raw == 0x2A) {
            return;
        }

        switch (raw) {
            case 0x1C: keycode = KEY_KP_ENTER;  if (pressed) ascii = '\n'; break;
            case 0x1D: keycode = KEY_RCTRL;     break;
            case 0x35: keycode = KEY_KP_DIVIDE; if (pressed) ascii = '/';  break;
            case 0x37: keycode = KEY_PRTSCN;    break;
            case 0x38: keycode = KEY_RALT;      break;
            case 0x47: keycode = KEY_HOME;      break;
            case 0x48: keycode = KEY_UP;        break;
            case 0x49: keycode = KEY_PGUP;      break;
            case 0x4B: keycode = KEY_LEFT;      break;
            case 0x4D: keycode = KEY_RIGHT;     break;
            case 0x4F: keycode = KEY_END;       break;
            case 0x50: keycode = KEY_DOWN;      break;
            case 0x51: keycode = KEY_PGDN;      break;
            case 0x52: keycode = KEY_INSERT;    break;
            case 0x53: keycode = KEY_DELETE;    break;
            case 0x5B: keycode = KEY_LGUI;      break;
            case 0x5C: keycode = KEY_RGUI;      break;
            case 0x5D: keycode = KEY_MENU;      break;
            default:   keycode = (uint16_t) (KEY_EXT_BASE + raw); break;
        }
    } else {
        switch (raw) {
            case KEY_CAPSLOCK:
                if (pressed) {
                    locks ^= KBD_LOCK_CAPS;
                    kbd_update_leds();
                }
                break;
            case KEY_NUMLOCK:
                if (pressed) {
                    locks ^= KBD_LOCK_NUM;
                    kbd_update_leds();
                }
                break;
            case KEY_SCROLLLOCK:
                if (pressed) {
                    locks ^= KBD_LOCK_SCROLL;
                    kbd_update_leds();
                }
                break;
            case 0x4A:
                if (pressed) ascii = '-';
                break;
            case 0x4E:
                if (pressed) ascii = '+';
                break;
            default:
                if (raw >= 0x47 && raw <= 0x53) {
                    if (locks & KBD_LOCK_NUM) {
                        if (pressed) ascii = kbd_us_ascii_lower[raw];
                    } else {
                        keycode = (uint16_t) (KEY_EXT_BASE + raw);
                    }
                } else if (pressed) {
                    ascii = translate_ascii(raw, (modifiers & KBD_MOD_SHIFT) != 0,
                                                  (locks & KBD_LOCK_CAPS) != 0);
                }
                break;
        }
    }

    switch (keycode) {
        case KEY_LSHIFT: set_mod(KBD_MOD_LSHIFT, pressed); break;
        case KEY_RSHIFT: set_mod(KBD_MOD_RSHIFT, pressed); break;
        case KEY_LCTRL:  set_mod(KBD_MOD_LCTRL,  pressed); break;
        case KEY_RCTRL:  set_mod(KBD_MOD_RCTRL,  pressed); break;
        case KEY_LALT:   set_mod(KBD_MOD_LALT,   pressed); break;
        case KEY_RALT:   set_mod(KBD_MOD_RALT,   pressed); break;
        case KEY_LGUI:   set_mod(KBD_MOD_LGUI,   pressed); break;
        case KEY_RGUI:   set_mod(KBD_MOD_RGUI,   pressed); break;
        default: break;
    }

    if (pressed && keycode == KEY_DELETE &&
        (modifiers & KBD_MOD_CTRL) && (modifiers & KBD_MOD_ALT)) {
        kbd_wait_input_clear();
        outb(KBD_COMMAND_PORT, 0xFE);
    }

    push_event(sc, keycode, ascii, pressed);
}

void kbd_init(void)
{
    modifiers = 0;
    locks = 0;
    extended = 0;
    pause_bytes_left = 0;
    event_head = 0;
    event_tail = 0;

    kbd_update_leds();

    register_interrupt_handler(IRQ1_VECTOR, keyboard_irq_handler);
    pic_clear_mask(1);
}

int kbd_read_event(kbd_event_t* out)
{
    if (event_head == event_tail) {
        return 0;
    }

    *out = event_queue[event_tail];
    event_tail = (uint8_t) ((event_tail + 1) % KBD_EVENT_QUEUE_SIZE);
    return 1;
}

int kbd_has_event(void)
{
    return event_head != event_tail;
}

uint8_t kbd_get_modifiers(void)
{
    return modifiers;
}

uint8_t kbd_get_locks(void)
{
    return locks;
}
