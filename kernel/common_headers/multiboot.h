#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define MULTIBOOT_FLAG_MEM      (1u << 0)
#define MULTIBOOT_FLAG_BOOTDEV  (1u << 1)
#define MULTIBOOT_FLAG_CMDLINE  (1u << 2)
#define MULTIBOOT_FLAG_MODS     (1u << 3)
#define MULTIBOOT_FLAG_AOUT     (1u << 4)
#define MULTIBOOT_FLAG_ELF      (1u << 5)
#define MULTIBOOT_FLAG_MMAP     (1u << 6)
#define MULTIBOOT_FLAG_LOADER   (1u << 9)

typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} multiboot_info_t;

typedef struct __attribute__((packed)) {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} multiboot_module_t;

typedef struct __attribute__((packed)) {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} multiboot_mmap_entry_t;

#define MULTIBOOT_MMAP_AVAILABLE 1
#define MULTIBOOT_MMAP_RESERVED  2
#define MULTIBOOT_MMAP_ACPI      3
#define MULTIBOOT_MMAP_NVS       4
#define MULTIBOOT_MMAP_BAD       5

#endif
