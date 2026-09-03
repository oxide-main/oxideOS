#include <stdint.h>
#include "drivers/vga.h"
#include "drivers/ps2kbd.h"
#include "drivers/acpi.h"
#include "drivers/pit.h"
#include "common_headers/io.h"
#include "common_headers/multiboot.h"
#include "arch/x86/gdt.h"
#include "arch/x86/idt.h"
#include "arch/x86/pmm.h"
#include "arch/x86/heap.h"
#include "arch/x86/paging.h"
#include "arch/x86/memory_test.h"

#define OXIDE_VERSION "0.1.0"
#define INPUT_MAX 128

static uint8_t term_color;
static int term_row;
static int term_col;
static uint32_t boot_magic;
static multiboot_info_t* boot_info;

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

static void term_move_cursor(void)
{
    update_cursor(term_col, term_row);
}

static void term_clear(void)
{
    vga_clean_screen();
    term_row = 0;
    term_col = 0;
    term_move_cursor();
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

static void write_u32_hex(uint32_t value)
{
    char digits[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        term_putchar(digits[(value >> (i * 4)) & 0xF]);
    }
}

static void reboot(void)
{
    term_writeln("rebooting...");
    __asm__ volatile ("cli");
    while (inb(KBD_STATUS_PORT) & 0x02) { }
    outb(KBD_COMMAND_PORT, 0xFE);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void halt(void)
{
    term_writeln("halted");
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void shutdown(void)
{
    acpi_power_status_t status;

    term_writeln("requesting ACPI shutdown...");
    status = acpi_poweroff();
    if (status != ACPI_POWER_OK) {
        term_write("ACPI shutdown unavailable: ");
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

static void cmd_help(void)
{
    term_writeln("commands:");
    term_writeln("  help      list commands");
    term_writeln("  clear     clear the screen");
    term_writeln("  echo      print arguments");
    term_writeln("  about     show project information");
    term_writeln("  version   show kernel version");
    term_writeln("  uname     show system name");
    term_writeln("  cpuinfo   show CPU details from CPUID");
    term_writeln("  meminfo   show Multiboot memory information");
    term_writeln("  memtest   run memory manager and heap self-tests");
    term_writeln("  uptime    show system uptime");
    term_writeln("  keymap    show keyboard layout");
    term_writeln("  keys      show modifier and lock state");
    term_writeln("  reboot    warm reboot through the PS/2 controller");
    term_writeln("  shutdown  request ACPI S5 poweroff, then halt");
    term_writeln("  halt      stop the CPU");
}

static void cmd_about(void)
{
    term_writeln("oxideOS is a small 32-bit x86 OSDev kernel.");
    term_writeln("Boot: Limine using the Multiboot1 protocol.");
    term_writeln("Drivers: VGA text output, GDT/IDT/ISR/PIC, PS/2 keyboard.");
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

    term_write("vendor: ");
    term_writeln(vendor);
    term_write("family: ");
    write_u32_dec(family);
    term_write("  model: ");
    write_u32_dec(model);
    term_write("  stepping: ");
    write_u32_dec(stepping);
    term_putchar('\n');
}

static void cmd_meminfo(void)
{
    if (boot_magic != MULTIBOOT_BOOTLOADER_MAGIC || boot_info == 0 ||
        (boot_info->flags & MULTIBOOT_FLAG_MEM) == 0) {
        term_writeln("Multiboot memory information is unavailable.");
        return;
    }

    term_write("lower memory: ");
    write_u32_dec(boot_info->mem_lower);
    term_writeln(" KiB");
    term_write("upper memory: ");
    write_u32_dec(boot_info->mem_upper);
    term_writeln(" KiB");
    term_write("total conventional+upper: ");
    write_u32_dec((boot_info->mem_lower + boot_info->mem_upper) / 1024);
    term_writeln(" MiB");

    uint32_t total  = (uint32_t) pmm_total_frames();
    uint32_t usable = (uint32_t) pmm_usable_frames();
    uint32_t used   = (uint32_t) pmm_used_frames();
    uint32_t free   = (uint32_t) pmm_free_frames();

    term_writeln("physical memory manager (PMM):");
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

    term_writeln("virtual memory (paging):");
    term_write("  status:          ");
    term_writeln(paging_is_enabled() ? "enabled" : "disabled");
    term_write("  page directory:  0x");
    write_u32_hex(paging_get_directory_phys());
    term_putchar('\n');

    term_writeln("kernel heap:");
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
    term_writeln("running memory allocator tests...");
    if (memory_run_tests()) {
        term_writeln("all memory tests PASSED.");
    } else {
        term_writeln("memory tests FAILED!");
    }
}

static void cmd_keys(void)
{
    uint8_t mods = kbd_get_modifiers();
    uint8_t locks = kbd_get_locks();

    term_write("modifiers: shift=");
    term_write((mods & KBD_MOD_SHIFT) ? "on" : "off");
    term_write(" ctrl=");
    term_write((mods & KBD_MOD_CTRL) ? "on" : "off");
    term_write(" alt=");
    term_write((mods & KBD_MOD_ALT) ? "on" : "off");
    term_write(" gui=");
    term_writeln((mods & KBD_MOD_GUI) ? "on" : "off");

    term_write("locks: caps=");
    term_write((locks & KBD_LOCK_CAPS) ? "on" : "off");
    term_write(" num=");
    term_write((locks & KBD_LOCK_NUM) ? "on" : "off");
    term_write(" scroll=");
    term_writeln((locks & KBD_LOCK_SCROLL) ? "on" : "off");
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
    } else if (streq_word(cmd, "help")) {
        cmd_help();
    } else if (streq_word(cmd, "clear")) {
        term_clear();
    } else if (streq_word(cmd, "echo")) {
        term_writeln(skip_spaces(args));
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
    } else if (streq_word(cmd, "uptime")) {
        uint32_t ms = pit_get_uptime_ms();
        uint32_t secs = ms / 1000;
        uint32_t frac = (ms % 1000) / 10;
        term_write("uptime: ");
        write_u32_dec(secs);
        term_putchar('.');
        if (frac < 10) {
            term_putchar('0');
        }
        write_u32_dec(frac);
        term_writeln(" seconds");
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
        term_write("unknown command: ");
        term_writeln(cmd);
        term_writeln("type 'help' for a command list");
    }
}

static void prompt(void)
{
    term_write("oxideOS> ");
}

static void shell(void)
{
    char input[INPUT_MAX];
    int len = 0;

    prompt();

    for (;;) {
        kbd_event_t ev;

        while (!kbd_has_event()) {
            __asm__ volatile ("hlt");
        }

        while (kbd_read_event(&ev)) {
            if (!ev.pressed || ev.ascii == 0) {
                continue;
            }

            if (ev.ascii == '\n') {
                term_putchar('\n');
                input[len] = '\0';
                execute_command(input);
                len = 0;
                prompt();
            } else if (ev.ascii == '\b') {
                if (len > 0) {
                    len--;
                    term_putchar('\b');
                }
            } else if (ev.ascii >= ' ' && ev.ascii <= '~') {
                if (len < INPUT_MAX - 1) {
                    input[len++] = ev.ascii;
                    term_putchar(ev.ascii);
                }
            }
        }
    }
}

void kernel_main(uint32_t magic, uint32_t multiboot_addr)
{
    term_color = vga_entry_color(WHITE, BLACK);
    boot_magic = magic;
    boot_info = (multiboot_info_t*) multiboot_addr;

    gdt_init();
    idt_init();
    pmm_init(boot_info, multiboot_addr);
    heap_init();
    paging_init(boot_info, multiboot_addr);
    if (!memory_run_tests()) {
        term_writeln("FATAL: Memory allocator self-test failed at boot!");
        halt();
    }
    kbd_init();
    pit_init();
    acpi_init();
    __asm__ volatile ("sti");

    term_clear();
    term_writeln("oxideOS kernel: " OXIDE_VERSION);
    term_writeln("Booted by limine.");
    term_writeln("Type 'help' for available commands.");
    term_writeln("Created and maintained by Johan & Pranav");
    term_putchar('\n');

    shell();
}
