#include <stdint.h>
#include "drivers/vga.h"
#include "drivers/ps2kbd.h"
#include "drivers/acpi.h"
#include "drivers/pit.h"
#include "common_headers/io.h"
#include "common_headers/string.h"
#include "common_headers/multiboot.h"
#include "arch/x86/gdt.h"
#include "arch/x86/idt.h"
#include "arch/x86/pmm.h"
#include "arch/x86/heap.h"
#include "arch/x86/paging.h"
#include "arch/x86/memory_test.h"

#define OXIDE_VERSION "0.2.0"
#define INPUT_MAX     128
#define HISTORY_MAX   16

static uint8_t term_color;
static uint8_t term_theme = LIGHT_CYAN;
static int term_row;
static int term_col;
static uint32_t boot_magic;
static multiboot_info_t* boot_info;

static char history[HISTORY_MAX][INPUT_MAX];
static int history_count = 0;
static int history_head  = 0;

static int streq_word(const char* cmd, const char* word)
{
    int i = 0;
    while (word[i] != '\0') {
        if (cmd[i] != word[i]) {
            return 0;
        }
        i++;
    }
    return cmd[i] == '\0' || cmd[i] == ' ' || cmd[i] == '\t';
}

static const char* skip_spaces(const char* s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int parse_int(const char* s, int32_t* out)
{
    int neg = 0;
    int32_t value = 0;
    int digits = 0;

    s = skip_spaces(s);
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        digits++;
        s++;
    }

    if (digits == 0) {
        return 0;
    }

    *out = neg ? -value : value;
    return 1;
}

static void term_move_cursor(void)
{
    update_cursor(term_col, term_row);
}

static void term_newline(void)
{
    term_col = 0;
    term_row++;
    if (term_row >= VGA_HEIGHT) {
        vga_scroll();
        term_row = VGA_HEIGHT - 1;
    }
    term_move_cursor();
}

static void term_putchar(char c)
{
    if (c == '\n') {
        term_newline();
        return;
    }

    if (c == '\r') {
        term_col = 0;
        term_move_cursor();
        return;
    }

    if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            vga_put_char(' ', term_color, term_col, term_row);
            term_move_cursor();
        }
        return;
    }

    vga_put_char(c, term_color, term_col, term_row);
    term_col++;
    if (term_col >= VGA_WIDTH) {
        term_newline();
    } else {
        term_move_cursor();
    }
}

static void term_write(const char* s)
{
    for (int i = 0; s[i] != '\0'; i++) {
        term_putchar(s[i]);
    }
}

static void term_writeln(const char* s)
{
    term_write(s);
    term_putchar('\n');
}

static void term_write_c(const char* s, uint8_t fg)
{
    uint8_t saved = term_color;
    term_color = vga_entry_color(fg, BLACK);
    term_write(s);
    term_color = saved;
}

static void term_writeln_c(const char* s, uint8_t fg)
{
    term_write_c(s, fg);
    term_putchar('\n');
}

static void term_clear(void)
{
    vga_clean_screen();
    term_row = 0;
    term_col = 0;
    term_move_cursor();
}

static void write_u32_dec(uint32_t value)
{
    char buf[11];
    int i = 0;

    if (value == 0) {
        term_putchar('0');
        return;
    }

    while (value > 0 && i < (int) sizeof(buf)) {
        buf[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        term_putchar(buf[--i]);
    }
}

static void write_i32_dec(int32_t value)
{
    if (value < 0) {
        term_putchar('-');
        write_u32_dec((uint32_t) (-(value + 1)) + 1);
    } else {
        write_u32_dec((uint32_t) value);
    }
}

static void write_u32_hex(uint32_t value)
{
    char digits[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        term_putchar(digits[(value >> (i * 4)) & 0xF]);
    }
}

static void boot_step(const char* label)
{
    term_write_c("  [", DARK_GREY);
    term_write_c(" OK ", LIGHT_GREEN);
    term_write_c("] ", DARK_GREY);
    term_writeln_c(label, LIGHT_GREY);
}

static void reboot(void)
{
    term_writeln_c("rebooting...", YELLOW);
    __asm__ volatile ("cli");
    while (inb(KBD_STATUS_PORT) & 0x02) { }
    outb(KBD_COMMAND_PORT, 0xFE);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void halt(void)
{
    term_writeln_c("halted", LIGHT_RED);
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void shutdown(void)
{
    acpi_power_status_t status;

    term_writeln_c("requesting ACPI shutdown...", YELLOW);
    status = acpi_poweroff();
    if (status != ACPI_POWER_OK) {
        term_write_c("ACPI shutdown unavailable: ", LIGHT_RED);
        term_writeln(acpi_power_status_string(status));
    }
    halt();
}

static void cpu_vendor(char out[13])
{
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;

    __asm__ volatile (
        "cpuid"
        : "=b"(ebx), "=d"(edx), "=c"(ecx)
        : "a"(0)
    );

    out[0] = (char) (ebx & 0xFF);
    out[1] = (char) ((ebx >> 8) & 0xFF);
    out[2] = (char) ((ebx >> 16) & 0xFF);
    out[3] = (char) ((ebx >> 24) & 0xFF);
    out[4] = (char) (edx & 0xFF);
    out[5] = (char) ((edx >> 8) & 0xFF);
    out[6] = (char) ((edx >> 16) & 0xFF);
    out[7] = (char) ((edx >> 24) & 0xFF);
    out[8] = (char) (ecx & 0xFF);
    out[9] = (char) ((ecx >> 8) & 0xFF);
    out[10] = (char) ((ecx >> 16) & 0xFF);
    out[11] = (char) ((ecx >> 24) & 0xFF);
    out[12] = '\0';
}

static void print_rule(uint8_t color)
{
    char bar[VGA_WIDTH + 1];
    int i;
    for (i = 0; i < VGA_WIDTH - 2; i++) {
        bar[i] = '\xCD';
    }
    bar[i] = '\0';
    term_write_c("  ", color);
    term_writeln_c(bar, color);
}

static void print_banner(void)
{
    static const uint8_t rainbow[] = {
        LIGHT_RED, YELLOW, LIGHT_GREEN, LIGHT_CYAN, LIGHT_BLUE, LIGHT_MAGENTA
    };
    static const char* word = "oxideOS";

    print_rule(DARK_GREY);

    term_write("  ");
    for (int i = 0; word[i] != '\0'; i++) {
        char c[2] = { word[i], '\0' };
        term_write_c(c, rainbow[i % (int) sizeof(rainbow)]);
    }
    term_write_c("  v" OXIDE_VERSION, DARK_GREY);
    term_putchar('\n');

    term_write("  ");
    term_writeln_c("a small 32-bit x86 hobby kernel", LIGHT_GREY);

    print_rule(DARK_GREY);
}

static uint8_t color_from_name(const char* name)
{
    if (strcmp(name, "red") == 0) return LIGHT_RED;
    if (strcmp(name, "green") == 0) return LIGHT_GREEN;
    if (strcmp(name, "blue") == 0) return LIGHT_BLUE;
    if (strcmp(name, "cyan") == 0) return LIGHT_CYAN;
    if (strcmp(name, "magenta") == 0) return LIGHT_MAGENTA;
    if (strcmp(name, "yellow") == 0) return YELLOW;
    if (strcmp(name, "white") == 0) return WHITE;
    if (strcmp(name, "grey") == 0 || strcmp(name, "gray") == 0) return LIGHT_GREY;
    if (strcmp(name, "brown") == 0) return BROWN;
    return 0xFF;
}

static void cmd_color(const char* args)
{
    args = skip_spaces(args);

    if (*args == '\0' || streq_word(args, "list")) {
        term_writeln("available colours:");
        term_write_c("  red     ", LIGHT_RED);
        term_write_c("yellow   ", YELLOW);
        term_write_c("green    ", LIGHT_GREEN);
        term_putchar('\n');
        term_write_c("  cyan    ", LIGHT_CYAN);
        term_write_c("blue     ", LIGHT_BLUE);
        term_write_c("magenta  ", LIGHT_MAGENTA);
        term_putchar('\n');
        term_write_c("  white   ", WHITE);
        term_write_c("grey     ", LIGHT_GREY);
        term_write_c("brown    ", BROWN);
        term_putchar('\n');
        term_writeln("usage: color <name>");
        return;
    }

    uint8_t fg = color_from_name(args);
    if (fg == 0xFF) {
        term_write("unknown colour: ");
        term_writeln(args);
        term_writeln("try 'color list'");
        return;
    }

    term_theme = fg;
    term_color = vga_entry_color(term_theme, BLACK);
    term_writeln_c("theme colour updated.", term_theme);
}

static void cmd_help(void)
{
    static const char* rows[][2] = {
        { "help",     "list commands" },
        { "clear",    "clear the screen" },
        { "banner",   "reprint the startup banner" },
        { "echo",     "print arguments" },
        { "color",    "change the shell theme colour ('color list')" },
        { "about",    "show project information" },
        { "version",  "show kernel version" },
        { "uname",    "show system name" },
        { "cpuinfo",  "show CPU details from CPUID" },
        { "meminfo",  "show Multiboot / PMM / heap / paging info" },
        { "memtest",  "run memory manager and heap self-tests" },
        { "neofetch", "show a colourful system summary" },
        { "uptime",   "show system uptime" },
        { "calc",     "evaluate 'calc <a> <op> <b>' (+ - * /)" },
        { "history",  "show recently run commands" },
        { "keymap",   "show keyboard layout" },
        { "keys",     "show modifier and lock state" },
        { "reboot",   "warm reboot through the PS/2 controller" },
        { "shutdown", "request ACPI S5 poweroff, then halt" },
        { "halt",     "stop the CPU" },
    };
    int n = (int) (sizeof(rows) / sizeof(rows[0]));

    term_writeln_c("commands:", term_theme);
    for (int i = 0; i < n; i++) {
        term_write("  ");
        term_write_c(rows[i][0], LIGHT_GREEN);
        int pad = 10 - (int) strlen(rows[i][0]);
        for (int p = 0; p < pad; p++) {
            term_putchar(' ');
        }
        term_writeln(rows[i][1]);
    }
}

static void cmd_about(void)
{
    term_writeln_c("oxideOS", LIGHT_CYAN);
    term_writeln("  A small 32-bit x86 OSDev kernel.");
    term_writeln("  Boot:    Limine, Multiboot1 protocol.");
    term_writeln("  Drivers: VGA text output, GDT/IDT/ISR/PIC, PS/2 keyboard,");
    term_writeln("           PIT timer, ACPI power management.");
    term_writeln("  Memory:  Physical frame allocator, paging, kernel heap.");
    term_write("  Authors: ");
    term_writeln_c("Johan & Pranav", YELLOW);
}

static void cmd_cpuinfo(void)
{
    char vendor[13];
    uint32_t eax;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;

    cpu_vendor(vendor);
    __asm__ volatile (
        "cpuid"
        : "=a"(eax)
        : "a"(1)
        : "ebx", "ecx", "edx"
    );

    stepping = eax & 0xF;
    model = (eax >> 4) & 0xF;
    family = (eax >> 8) & 0xF;

    term_write_c("vendor: ", LIGHT_GREY);
    term_writeln_c(vendor, LIGHT_GREEN);
    term_write_c("family: ", LIGHT_GREY);
    write_u32_dec(family);
    term_write_c("  model: ", LIGHT_GREY);
    write_u32_dec(model);
    term_write_c("  stepping: ", LIGHT_GREY);
    write_u32_dec(stepping);
    term_putchar('\n');
}

static void cmd_meminfo(void)
{
    if (boot_magic != MULTIBOOT_BOOTLOADER_MAGIC || boot_info == 0 ||
        (boot_info->flags & MULTIBOOT_FLAG_MEM) == 0) {
        term_writeln_c("Multiboot memory information is unavailable.", LIGHT_RED);
        return;
    }

    term_write_c("lower memory: ", LIGHT_GREY);
    write_u32_dec(boot_info->mem_lower);
    term_writeln(" KiB");
    term_write_c("upper memory: ", LIGHT_GREY);
    write_u32_dec(boot_info->mem_upper);
    term_writeln(" KiB");
    term_write_c("total conventional+upper: ", LIGHT_GREY);
    write_u32_dec((boot_info->mem_lower + boot_info->mem_upper) / 1024);
    term_writeln(" MiB");

    uint32_t total  = (uint32_t) pmm_total_frames();
    uint32_t usable = (uint32_t) pmm_usable_frames();
    uint32_t used   = (uint32_t) pmm_used_frames();
    uint32_t free   = (uint32_t) pmm_free_frames();

    term_writeln_c("physical memory manager (PMM):", LIGHT_CYAN);
    term_write("  total physical:  ");
    write_u32_dec(total);
    term_write(" frames (");
    write_u32_dec(total * 4 / 1024);
    term_writeln(" MiB)");
    term_write("  usable memory:   ");
    write_u32_dec(usable);
    term_write(" frames (");
    write_u32_dec(usable * 4 / 1024);
    term_writeln(" MiB)");
    term_write("  used memory:     ");
    write_u32_dec(used);
    term_write(" frames (");
    write_u32_dec(used * 4);
    term_writeln(" KiB)");
    term_write("  free memory:     ");
    write_u32_dec(free);
    term_write(" frames (");
    write_u32_dec(free * 4 / 1024);
    term_writeln(" MiB)");

    term_writeln_c("virtual memory (paging):", LIGHT_CYAN);
    term_write("  status:          ");
    term_writeln(paging_is_enabled() ? "enabled" : "disabled");
    term_write("  page directory:  0x");
    write_u32_hex(paging_get_directory_phys());
    term_putchar('\n');

    term_writeln_c("kernel heap:", LIGHT_CYAN);
    term_write("  managed: ");
    write_u32_dec(heap_total_bytes());
    term_writeln(" bytes");
    term_write("  used:    ");
    write_u32_dec(heap_used_bytes());
    term_writeln(" bytes");
    term_write("  free:    ");
    write_u32_dec(heap_free_bytes());
    term_writeln(" bytes");
}

static void cmd_memtest(void)
{
    term_writeln_c("running memory allocator tests...", YELLOW);
    if (memory_run_tests()) {
        term_writeln_c("all memory tests PASSED.", LIGHT_GREEN);
    } else {
        term_writeln_c("memory tests FAILED!", LIGHT_RED);
    }
}

static void cmd_keys(void)
{
    uint8_t mods = kbd_get_modifiers();
    uint8_t locks = kbd_get_locks();

    term_write_c("modifiers: ", LIGHT_GREY);
    term_write("shift=");
    term_write((mods & KBD_MOD_SHIFT) ? "on" : "off");
    term_write(" ctrl=");
    term_write((mods & KBD_MOD_CTRL) ? "on" : "off");
    term_write(" alt=");
    term_write((mods & KBD_MOD_ALT) ? "on" : "off");
    term_write(" gui=");
    term_writeln((mods & KBD_MOD_GUI) ? "on" : "off");

    term_write_c("locks:     ", LIGHT_GREY);
    term_write("caps=");
    term_write((locks & KBD_LOCK_CAPS) ? "on" : "off");
    term_write(" num=");
    term_write((locks & KBD_LOCK_NUM) ? "on" : "off");
    term_write(" scroll=");
    term_writeln((locks & KBD_LOCK_SCROLL) ? "on" : "off");
}

static void write_uptime(void)
{
    uint32_t ms = pit_get_uptime_ms();
    uint32_t secs = ms / 1000;
    uint32_t frac = (ms % 1000) / 10;
    write_u32_dec(secs);
    term_putchar('.');
    if (frac < 10) {
        term_putchar('0');
    }
    write_u32_dec(frac);
    term_write("s");
}

static void cmd_neofetch(void)
{
    char vendor[13];
    cpu_vendor(vendor);

    uint32_t total  = (uint32_t) pmm_total_frames() * 4 / 1024;
    uint32_t used   = (uint32_t) pmm_used_frames() * 4 / 1024;

    static const uint8_t rainbow[] = {
        LIGHT_RED, YELLOW, LIGHT_GREEN, LIGHT_CYAN, LIGHT_BLUE, LIGHT_MAGENTA
    };
    static const char* word = "oxideOS";

    term_write("  ");
    for (int i = 0; word[i] != '\0'; i++) {
        char c[2] = { word[i], '\0' };
        term_write_c(c, rainbow[i % (int) sizeof(rainbow)]);
    }
    term_write("@localhost");
    term_putchar('\n');
    term_write("  ");
    for (int i = 0; i < 18; i++) {
        term_putchar('-');
    }
    term_putchar('\n');

    term_write("  ");
    term_write_c("OS:      ", LIGHT_CYAN);
    term_writeln("oxideOS i386 " OXIDE_VERSION);

    term_write("  ");
    term_write_c("Kernel:  ", LIGHT_CYAN);
    term_writeln("oxideOS/kernel.c (Multiboot1, Limine)");

    term_write("  ");
    term_write_c("CPU:     ", LIGHT_CYAN);
    term_writeln(vendor);

    term_write("  ");
    term_write_c("Memory:  ", LIGHT_CYAN);
    write_u32_dec(used);
    term_write(" / ");
    write_u32_dec(total);
    term_writeln(" MiB");

    term_write("  ");
    term_write_c("Paging:  ", LIGHT_CYAN);
    term_writeln(paging_is_enabled() ? "enabled" : "disabled");

    term_write("  ");
    term_write_c("Uptime:  ", LIGHT_CYAN);
    write_uptime();
    term_putchar('\n');

    term_write("  ");
    term_write_c("Shell:   ", LIGHT_CYAN);
    term_writeln("oxideOS built-in shell");

    term_write("  ");
    term_write_c("Theme:   ", LIGHT_CYAN);
    for (uint32_t i = 0; i < sizeof(rainbow); i++) {
        term_write_c("\xDB", rainbow[i]);
    }
    term_putchar('\n');
}

static void cmd_calc(const char* args)
{
    args = skip_spaces(args);

    int32_t a;
    if (!parse_int(args, &a)) {
        term_writeln("usage: calc <a> <op> <b>   (op is one of + - * /)");
        return;
    }

    while (*args == '-' || *args == '+' || (*args >= '0' && *args <= '9')) {
        args++;
    }
    args = skip_spaces(args);

    char op = *args;
    if (op != '+' && op != '-' && op != '*' && op != '/') {
        term_writeln("usage: calc <a> <op> <b>   (op is one of + - * /)");
        return;
    }
    args++;
    args = skip_spaces(args);

    int32_t b;
    if (!parse_int(args, &b)) {
        term_writeln("usage: calc <a> <op> <b>   (op is one of + - * /)");
        return;
    }

    write_i32_dec(a);
    term_putchar(' ');
    term_putchar(op);
    term_putchar(' ');
    write_i32_dec(b);
    term_write_c(" = ", LIGHT_GREY);

    switch (op) {
        case '+':
            write_i32_dec(a + b);
            break;
        case '-':
            write_i32_dec(a - b);
            break;
        case '*':
            write_i32_dec(a * b);
            break;
        case '/':
            if (b == 0) {
                term_write_c("error: division by zero", LIGHT_RED);
            } else {
                write_i32_dec(a / b);
            }
            break;
    }
    term_putchar('\n');
}

static void history_push(const char* line)
{
    if (line[0] == '\0') {
        return;
    }

    if (history_count > 0) {
        int last = (history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
        if (strcmp(history[last], line) == 0) {
            return;
        }
    }

    int i = 0;
    while (i < INPUT_MAX - 1 && line[i] != '\0') {
        history[history_head][i] = line[i];
        i++;
    }
    history[history_head][i] = '\0';

    history_head = (history_head + 1) % HISTORY_MAX;
    if (history_count < HISTORY_MAX) {
        history_count++;
    }
}

static const char* history_get(int steps_back)
{
    int idx = (history_head - 1 - steps_back + HISTORY_MAX * 4) % HISTORY_MAX;
    return history[idx];
}

static void cmd_history(void)
{
    if (history_count == 0) {
        term_writeln("no commands run yet.");
        return;
    }

    for (int i = history_count - 1; i >= 0; i--) {
        term_write_c("  ", LIGHT_GREY);
        write_u32_dec((uint32_t) (history_count - i));
        term_write_c("  ", LIGHT_GREY);
        term_writeln(history_get(i));
    }
}

static void execute_command(char* line)
{
    const char* cmd = skip_spaces(line);
    const char* args = cmd;

    while (*args != '\0' && *args != ' ' && *args != '\t') {
        args++;
    }

    if (*cmd == '\0') {
        return;
    }

    history_push(cmd);

    if (streq_word(cmd, "help")) {
        cmd_help();
    } else if (streq_word(cmd, "clear")) {
        term_clear();
    } else if (streq_word(cmd, "banner")) {
        print_banner();
    } else if (streq_word(cmd, "echo")) {
        term_writeln(skip_spaces(args));
    } else if (streq_word(cmd, "color")) {
        cmd_color(args);
    } else if (streq_word(cmd, "about")) {
        cmd_about();
    } else if (streq_word(cmd, "version")) {
        term_writeln(OXIDE_VERSION);
    } else if (streq_word(cmd, "uname")) {
        term_writeln("oxideOS i386");
    } else if (streq_word(cmd, "cpuinfo")) {
        cmd_cpuinfo();
    } else if (streq_word(cmd, "meminfo")) {
        cmd_meminfo();
    } else if (streq_word(cmd, "memtest")) {
        cmd_memtest();
    } else if (streq_word(cmd, "neofetch")) {
        cmd_neofetch();
    } else if (streq_word(cmd, "calc")) {
        cmd_calc(args);
    } else if (streq_word(cmd, "history")) {
        cmd_history();
    } else if (streq_word(cmd, "uptime")) {
        term_write("uptime: ");
        write_uptime();
        term_write_c(" (running)", LIGHT_GREEN);
        term_putchar('\n');
    } else if (streq_word(cmd, "keymap")) {
        term_writeln("keyboard: PS/2 Set 1, US QWERTY");
    } else if (streq_word(cmd, "keys")) {
        cmd_keys();
    } else if (streq_word(cmd, "reboot")) {
        reboot();
    } else if (streq_word(cmd, "shutdown")) {
        shutdown();
    } else if (streq_word(cmd, "halt")) {
        halt();
    } else {
        term_write_c("unknown command: ", LIGHT_RED);
        term_writeln(cmd);
        term_writeln("type 'help' for a command list");
    }
}

static void prompt(void)
{
    term_write_c("oxide", term_theme);
    term_write_c(">", DARK_GREY);
    term_write(" ");
}

static void redraw_input(char* input, int* len, const char* new_line)
{
    while (*len > 0) {
        term_putchar('\b');
        (*len)--;
    }

    int i = 0;
    while (new_line[i] != '\0' && i < INPUT_MAX - 1) {
        input[i] = new_line[i];
        term_putchar(new_line[i]);
        i++;
    }
    input[i] = '\0';
    *len = i;
}

static void shell(void)
{
    char input[INPUT_MAX];
    int len = 0;
    int hist_nav = -1;

    prompt();

    for (;;) {
        kbd_event_t ev;

        while (!kbd_has_event()) {
            __asm__ volatile ("hlt");
        }

        while (kbd_read_event(&ev)) {
            if (!ev.pressed) {
                continue;
            }

            if (ev.keycode == KEY_UP) {
                if (history_count > 0 && hist_nav + 1 < history_count) {
                    hist_nav++;
                    redraw_input(input, &len, history_get(hist_nav));
                }
                continue;
            }

            if (ev.keycode == KEY_DOWN) {
                if (hist_nav > 0) {
                    hist_nav--;
                    redraw_input(input, &len, history_get(hist_nav));
                } else if (hist_nav == 0) {
                    hist_nav = -1;
                    redraw_input(input, &len, "");
                }
                continue;
            }

            if (ev.ascii == 0) {
                continue;
            }

            if (ev.ascii == '\n') {
                term_putchar('\n');
                input[len] = '\0';
                execute_command(input);
                len = 0;
                hist_nav = -1;
                prompt();
            } else if (ev.ascii == '\b') {
                if (len > 0) {
                    len--;
                    term_putchar('\b');
                }
                hist_nav = -1;
            } else if (ev.ascii >= ' ' && ev.ascii <= '~') {
                if (len < INPUT_MAX - 1) {
                    input[len++] = ev.ascii;
                    term_putchar(ev.ascii);
                }
                hist_nav = -1;
            }
        }
    }
}

void kernel_main(uint32_t magic, uint32_t multiboot_addr)
{
    term_theme = LIGHT_CYAN;
    term_color = vga_entry_color(WHITE, BLACK);
    boot_magic = magic;
    boot_info = (multiboot_info_t*) multiboot_addr;

    term_clear();

    gdt_init();
    boot_step("Global Descriptor Table installed");
    idt_init();
    boot_step("Interrupt Descriptor Table installed");
    pmm_init(boot_info, multiboot_addr);
    boot_step("Physical memory manager initialised");
    heap_init();
    boot_step("Kernel heap initialised");
    paging_init(boot_info, multiboot_addr);
    boot_step("Paging enabled");
    if (!memory_run_tests()) {
        term_writeln_c("FATAL: Memory allocator self-test failed at boot!", LIGHT_RED);
        halt();
    }
    boot_step("Memory allocator self-test passed");
    kbd_init();
    boot_step("PS/2 keyboard driver ready");
    pit_init();
    boot_step("Programmable interval timer ready");
    acpi_init();
    boot_step("ACPI power management ready");
    __asm__ volatile ("sti");
    boot_step("Interrupts enabled");

    term_putchar('\n');
    print_banner();
    term_putchar('\n');

    term_write("Type ");
    term_write_c("help", LIGHT_GREEN);
    term_write(" for available commands, or ");
    term_write_c("neofetch", LIGHT_GREEN);
    term_writeln(" for a system summary.");
    term_write_c("Created and maintained by Johan & Pranav", DARK_GREY);
    term_putchar('\n');
    term_putchar('\n');

    term_color = vga_entry_color(term_theme, BLACK);
    shell();
}
