#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "isr.h"
#include "drivers/vga.h"
#include "common_headers/string.h"

extern uint8_t _kernel_start;
extern uint8_t _kernel_end;

typedef uint32_t page_directory_entry_t;
typedef uint32_t page_table_entry_t;

typedef struct page_table {
    page_table_entry_t entries[1024];
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

typedef struct page_directory {
    page_directory_entry_t entries[1024];
} __attribute__((aligned(PAGE_SIZE))) page_directory_t;

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
} acpi_early_rsdp_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oemid[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_early_sdt_header_t;

typedef struct __attribute__((packed)) {
    acpi_early_sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  model;
    uint8_t  profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved;
    uint32_t flags;
    uint8_t  reset_reg[12];
    uint8_t  reset_value;
    uint16_t arm_boot_arch;
    uint8_t  fadt_minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
} acpi_early_fadt_t;

static uintptr_t kernel_pd_phys;
static uintptr_t pt0_phys;
static int paging_active = 0;

void paging_invlpg(uintptr_t virtual_addr)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

int paging_is_enabled(void)
{
    return paging_active;
}

uintptr_t paging_get_directory_phys(void)
{
    return kernel_pd_phys;
}

static void print_panic_line(const char *label, uint32_t val, int row)
{
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (val >> (i * 4)) & 0x0F;
        buf[2 + (7 - i)] = nibble < 10 ? '0' + nibble : 'a' + (nibble - 10);
    }
    buf[10] = '\0';

    uint8_t color = vga_entry_color(RED, BLACK);
    int col = 0;
    for (int i = 0; label[i] != '\0'; i++) {
        vga_put_char(label[i], color, col++, row);
    }
    for (int i = 0; buf[i] != '\0'; i++) {
        vga_put_char(buf[i], color, col++, row);
    }
}

static void page_fault_handler(registers_t *regs)
{
    uint32_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    uint8_t color = vga_entry_color(RED, BLACK);

    vga_put_chars("[PANIC] Page Fault (#PF, Vector 14)", color, VGA_HEIGHT - 6);
    print_panic_line("  Fault address (CR2): ", cr2, VGA_HEIGHT - 5);
    print_panic_line("  Instruction   (EIP): ", regs->eip, VGA_HEIGHT - 4);
    print_panic_line("  Error code         : ", regs->err_code, VGA_HEIGHT - 3);

    uint32_t err = regs->err_code;
    const char *p_str = (err & (1u << 0)) ? "prot-violation" : "not-present";
    const char *w_str = (err & (1u << 1)) ? "write" : "read";
    const char *u_str = (err & (1u << 2)) ? "user" : "supervisor";
    const char *r_str = (err & (1u << 3)) ? " reserved-bit" : "";
    const char *i_str = (err & (1u << 4)) ? " ifetch" : "";

    char cause[64];
    int pos = 0;
    const char *prefix = "  Cause: ";
    for (int i = 0; prefix[i]; i++) cause[pos++] = prefix[i];
    for (int i = 0; p_str[i]; i++) cause[pos++] = p_str[i];
    cause[pos++] = ' ';
    for (int i = 0; w_str[i]; i++) cause[pos++] = w_str[i];
    cause[pos++] = ' ';
    for (int i = 0; u_str[i]; i++) cause[pos++] = u_str[i];
    for (int i = 0; r_str[i]; i++) cause[pos++] = r_str[i];
    for (int i = 0; i_str[i]; i++) cause[pos++] = i_str[i];
    cause[pos] = '\0';

    vga_put_chars(cause, color, VGA_HEIGHT - 2);
    vga_put_chars("System halted.", color, VGA_HEIGHT - 1);

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

int paging_map_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint32_t flags)
{
    if ((virtual_addr & 0xFFFu) != 0 || (physical_addr & 0xFFFu) != 0) {
        return -1;
    }
    if (virtual_addr == PAGING_SCRATCH_PAGE) {
        return -1;
    }

    uint32_t pde_idx = virtual_addr >> 22;
    uint32_t pte_idx = (virtual_addr >> 12) & 0x3FF;

    uint32_t cpu_flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(cpu_flags));

    page_directory_t *pd = (page_directory_t *) kernel_pd_phys;

    if (!(pd->entries[pde_idx] & PAGE_PRESENT)) {
        uintptr_t new_pt_phys = pmm_alloc_frame();
        if (new_pt_phys == 0) {
            __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
            return -1;
        }

        if (paging_active) {
            page_table_t *pt0 = (page_table_t *) pt0_phys;
            pt0->entries[1023] = (new_pt_phys & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_WRITABLE;
            paging_invlpg(PAGING_SCRATCH_PAGE);
            memset((void *) PAGING_SCRATCH_PAGE, 0, PAGE_SIZE);
        } else {
            memset((void *) new_pt_phys, 0, PAGE_SIZE);
        }

        uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITABLE;
        if (flags & PAGE_USER) {
            pde_flags |= PAGE_USER;
        }
        pd->entries[pde_idx] = (new_pt_phys & PAGE_FRAME_MASK) | pde_flags;
    } else {
        if (flags & PAGE_USER) {
            pd->entries[pde_idx] |= PAGE_USER;
        }
    }

    uintptr_t pt_phys = pd->entries[pde_idx] & PAGE_FRAME_MASK;
    page_table_t *pt;

    if (pde_idx == 0) {
        pt = (page_table_t *) pt0_phys;
    } else if (paging_active) {
        page_table_t *pt0 = (page_table_t *) pt0_phys;
        pt0->entries[1023] = (pt_phys & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_WRITABLE;
        paging_invlpg(PAGING_SCRATCH_PAGE);
        pt = (page_table_t *) PAGING_SCRATCH_PAGE;
    } else {
        pt = (page_table_t *) pt_phys;
    }

    if (pt->entries[pte_idx] & PAGE_PRESENT) {
        uintptr_t existing_frame = pt->entries[pte_idx] & PAGE_FRAME_MASK;
        uint32_t existing_flags = pt->entries[pte_idx] & PAGE_FLAGS_MASK;
        if (existing_frame == (physical_addr & PAGE_FRAME_MASK) &&
            existing_flags == (flags & PAGE_FLAGS_MASK)) {
            __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
            return 0;
        }
        __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
        return -2;
    }

    pt->entries[pte_idx] = (physical_addr & PAGE_FRAME_MASK) | (flags & PAGE_FLAGS_MASK) | PAGE_PRESENT;

    if (paging_active) {
        paging_invlpg(virtual_addr);
    }

    __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
    return 0;
}

void paging_unmap_page(uintptr_t virtual_addr)
{
    virtual_addr &= PAGE_FRAME_MASK;
    if (virtual_addr == PAGING_SCRATCH_PAGE) {
        return;
    }

    uint32_t pde_idx = virtual_addr >> 22;
    uint32_t pte_idx = (virtual_addr >> 12) & 0x3FF;

    uint32_t cpu_flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(cpu_flags));

    page_directory_t *pd = (page_directory_t *) kernel_pd_phys;
    if (!(pd->entries[pde_idx] & PAGE_PRESENT)) {
        __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
        return;
    }

    uintptr_t pt_phys = pd->entries[pde_idx] & PAGE_FRAME_MASK;
    page_table_t *pt;

    if (pde_idx == 0) {
        pt = (page_table_t *) pt0_phys;
    } else if (paging_active) {
        page_table_t *pt0 = (page_table_t *) pt0_phys;
        pt0->entries[1023] = (pt_phys & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_WRITABLE;
        paging_invlpg(PAGING_SCRATCH_PAGE);
        pt = (page_table_t *) PAGING_SCRATCH_PAGE;
    } else {
        pt = (page_table_t *) pt_phys;
    }

    pt->entries[pte_idx] = 0;

    if (paging_active) {
        paging_invlpg(virtual_addr);
    }

    __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
}

uintptr_t paging_get_physical(uintptr_t virtual_addr)
{
    uint32_t pde_idx = virtual_addr >> 22;
    uint32_t pte_idx = (virtual_addr >> 12) & 0x3FF;
    uint32_t offset  = virtual_addr & 0xFFFu;

    uint32_t cpu_flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(cpu_flags));

    page_directory_t *pd = (page_directory_t *) kernel_pd_phys;
    if (!(pd->entries[pde_idx] & PAGE_PRESENT)) {
        __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
        return (uintptr_t) -1;
    }

    uintptr_t pt_phys = pd->entries[pde_idx] & PAGE_FRAME_MASK;
    page_table_t *pt;

    if (pde_idx == 0) {
        pt = (page_table_t *) pt0_phys;
    } else if (paging_active) {
        page_table_t *pt0 = (page_table_t *) pt0_phys;
        pt0->entries[1023] = (pt_phys & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_WRITABLE;
        paging_invlpg(PAGING_SCRATCH_PAGE);
        pt = (page_table_t *) PAGING_SCRATCH_PAGE;
    } else {
        pt = (page_table_t *) pt_phys;
    }

    if (!(pt->entries[pte_idx] & PAGE_PRESENT)) {
        __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
        return (uintptr_t) -1;
    }

    uintptr_t phys = (pt->entries[pte_idx] & PAGE_FRAME_MASK) | offset;
    __asm__ volatile ("pushl %0; popfl" : : "r"(cpu_flags));
    return phys;
}

static void map_range(uintptr_t base, uintptr_t length)
{
    if (length == 0) {
        return;
    }
    uintptr_t start = base & PAGE_FRAME_MASK;
    uint64_t end = (uint64_t) base + length;
    uintptr_t end_page = (uintptr_t)((end + PAGE_SIZE - 1) & PAGE_FRAME_MASK);

    for (uintptr_t p = start; p < end_page; p += PAGE_SIZE) {
        paging_map_page(p, p, PAGE_PRESENT | PAGE_WRITABLE);
    }
}

static const acpi_early_rsdp_t *scan_rsdp(uint32_t start, uint32_t end)
{
    for (uint32_t addr = start; addr < end; addr += 16) {
        const acpi_early_rsdp_t *rsdp = (const acpi_early_rsdp_t *) addr;
        const char *sig = "RSD PTR ";
        int match = 1;
        for (int i = 0; i < 8; i++) {
            if (rsdp->signature[i] != sig[i]) {
                match = 0;
                break;
            }
        }
        if (match) {
            uint8_t sum = 0;
            const uint8_t *b = (const uint8_t *) rsdp;
            for (int i = 0; i < 20; i++) {
                sum = (uint8_t)(sum + b[i]);
            }
            if (sum == 0) {
                return rsdp;
            }
        }
    }
    return 0;
}

static void map_acpi_tables(void)
{
    uint16_t ebda_seg;
    __asm__ volatile ("movw (0x40E), %0" : "=r"(ebda_seg) : : "memory");
    uint32_t ebda = ((uint32_t) ebda_seg) << 4;

    const acpi_early_rsdp_t *rsdp = 0;
    if (ebda != 0) {
        rsdp = scan_rsdp(ebda, ebda + 1024);
    }
    if (!rsdp) {
        rsdp = scan_rsdp(0xE0000, 0x100000);
    }

    if (!rsdp || rsdp->rsdt_addr == 0) {
        return;
    }

    map_range(rsdp->rsdt_addr, sizeof(acpi_early_sdt_header_t));
    const acpi_early_sdt_header_t *rsdt = (const acpi_early_sdt_header_t *) rsdp->rsdt_addr;
    if (rsdt->length >= sizeof(acpi_early_sdt_header_t)) {
        map_range(rsdp->rsdt_addr, rsdt->length);
        uint32_t count = (rsdt->length - sizeof(acpi_early_sdt_header_t)) / 4;
        const uint32_t *entries = (const uint32_t *)(rsdt + 1);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t tbl_addr = entries[i];
            if (tbl_addr != 0) {
                map_range(tbl_addr, sizeof(acpi_early_sdt_header_t));
                const acpi_early_sdt_header_t *tbl = (const acpi_early_sdt_header_t *) tbl_addr;
                if (tbl->length >= sizeof(acpi_early_sdt_header_t)) {
                    map_range(tbl_addr, tbl->length);
                    if (tbl->signature[0] == 'F' && tbl->signature[1] == 'A' &&
                        tbl->signature[2] == 'C' && tbl->signature[3] == 'P') {
                        const acpi_early_fadt_t *fadt = (const acpi_early_fadt_t *) tbl;
                        if (fadt->dsdt != 0) {
                            map_range(fadt->dsdt, sizeof(acpi_early_sdt_header_t));
                            const acpi_early_sdt_header_t *dsdt = (const acpi_early_sdt_header_t *) fadt->dsdt;
                            if (dsdt->length >= sizeof(acpi_early_sdt_header_t)) {
                                map_range(fadt->dsdt, dsdt->length);
                            }
                        }
                        if (fadt->firmware_ctrl != 0) {
                            map_range(fadt->firmware_ctrl, 64);
                        }
                        if (fadt->header.length >= 148 && fadt->x_dsdt != 0 && (fadt->x_dsdt >> 32) == 0) {
                            map_range((uint32_t) fadt->x_dsdt, sizeof(acpi_early_sdt_header_t));
                            const acpi_early_sdt_header_t *xdsdt = (const acpi_early_sdt_header_t *)(uint32_t) fadt->x_dsdt;
                            if (xdsdt->length >= sizeof(acpi_early_sdt_header_t)) {
                                map_range((uint32_t) fadt->x_dsdt, xdsdt->length);
                            }
                        }
                    }
                }
            }
        }
    }
}

void paging_enable(void)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_pd_phys) : "memory");

    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

void paging_init(multiboot_info_t *mbi, uintptr_t multiboot_phys)
{
    kernel_pd_phys = pmm_alloc_frame();
    memset((void *) kernel_pd_phys, 0, PAGE_SIZE);

    pt0_phys = pmm_alloc_frame();
    memset((void *) pt0_phys, 0, PAGE_SIZE);

    page_directory_t *pd = (page_directory_t *) kernel_pd_phys;
    pd->entries[0] = (pt0_phys & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_WRITABLE;

    /*
     * 1. Low memory bootstrap mapping (0x00000000 - 0x00100000):
     * Mapped because BIOS BDA (0x40E for EBDA pointer), EBDA, VGA text buffer (0xB8000),
     * and BIOS ROM (0xE0000 - 0x100000 scanned for ACPI RSDP) reside in this physical range.
     */
    for (uintptr_t p = 0; p < 0x00100000; p += PAGE_SIZE) {
        paging_map_page(p, p, PAGE_PRESENT | PAGE_WRITABLE);
    }

    /*
     * 2. Kernel image mapping:
     * Covers .text, .rodata, .data, .bss, and the kernel stack.
     */
    uintptr_t k_start = (uintptr_t) &_kernel_start & PAGE_FRAME_MASK;
    uintptr_t k_end   = ((uintptr_t) &_kernel_end + PAGE_SIZE - 1) & PAGE_FRAME_MASK;
    for (uintptr_t p = k_start; p < k_end; p += PAGE_SIZE) {
        paging_map_page(p, p, PAGE_PRESENT | PAGE_WRITABLE);
    }

    /*
     * 3. Physical memory manager metadata:
     * Covers alloc_bitmap and reserved_bitmap placed after _kernel_end.
     */
    uintptr_t bm_start = pmm_get_bitmap_start() & PAGE_FRAME_MASK;
    uintptr_t bm_end   = (pmm_get_bitmap_end() + PAGE_SIZE - 1) & PAGE_FRAME_MASK;
    for (uintptr_t p = bm_start; p < bm_end; p += PAGE_SIZE) {
        paging_map_page(p, p, PAGE_PRESENT | PAGE_WRITABLE);
    }

    /*
     * 4. Initial paging structures:
     * Page directory and Page Table 0 themselves.
     */
    paging_map_page(kernel_pd_phys, kernel_pd_phys, PAGE_PRESENT | PAGE_WRITABLE);
    paging_map_page(pt0_phys, pt0_phys, PAGE_PRESENT | PAGE_WRITABLE);

    /*
     * 5. Multiboot structures:
     * Bootloader-provided tables, mmap, strings, and modules.
     */
    if (mbi && multiboot_phys) {
        map_range(multiboot_phys, sizeof(multiboot_info_t));
        if (mbi->flags & MULTIBOOT_FLAG_MMAP) {
            map_range(mbi->mmap_addr, mbi->mmap_length);
        }
        if ((mbi->flags & MULTIBOOT_FLAG_CMDLINE) && mbi->cmdline) {
            map_range(mbi->cmdline, strlen((const char *) mbi->cmdline) + 1);
        }
        if ((mbi->flags & MULTIBOOT_FLAG_LOADER) && mbi->boot_loader_name) {
            map_range(mbi->boot_loader_name, strlen((const char *) mbi->boot_loader_name) + 1);
        }
        if ((mbi->flags & MULTIBOOT_FLAG_MODS) && mbi->mods_count > 0 && mbi->mods_addr) {
            map_range(mbi->mods_addr, mbi->mods_count * sizeof(multiboot_module_t));
            multiboot_module_t *mods = (multiboot_module_t *) mbi->mods_addr;
            for (uint32_t m = 0; m < mbi->mods_count; m++) {
                if (mods[m].mod_end > mods[m].mod_start) {
                    map_range(mods[m].mod_start, mods[m].mod_end - mods[m].mod_start);
                }
                if (mods[m].cmdline) {
                    map_range(mods[m].cmdline, strlen((const char *) mods[m].cmdline) + 1);
                }
            }
        }

        /* Map non-RAM / ACPI regions from memory map */
        if ((mbi->flags & MULTIBOOT_FLAG_MMAP) && mbi->mmap_addr && mbi->mmap_length > 0) {
            uintptr_t mmap_end = mbi->mmap_addr + mbi->mmap_length;
            for (uintptr_t off = mbi->mmap_addr; off < mmap_end;) {
                multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *) off;
                if (entry->type != MULTIBOOT_MMAP_AVAILABLE && entry->addr < 0x100000000ULL) {
                    uint64_t len = entry->len;
                    if (entry->addr + len > 0x100000000ULL) {
                        len = 0x100000000ULL - entry->addr;
                    }
                    map_range((uintptr_t) entry->addr, (uintptr_t) len);
                }
                off += entry->size + 4;
            }
        }
    }

    /*
     * 6. ACPI tables:
     * Scan table headers before paging is enabled and identity-map their regions.
     */
    map_acpi_tables();

    /*
     * 7. Kernel heap initial allocations.
     */
    heap_map_all_blocks();

    /*
     * 8. Register Page Fault (#PF, vector 14) handler.
     */
    register_interrupt_handler(14, page_fault_handler);

    /*
     * 9. Load CR3 and enable paging bit in CR0.
     */
    paging_enable();
    paging_active = 1;
}

int paging_run_tests(void)
{
    /* 1. Identity mapping test */
    if (paging_get_physical(0x00100000) != 0x00100000) {
        return 0;
    }
    if (paging_get_physical(0x00100123) != 0x00100123) {
        return 0;
    }
    if (paging_get_physical(0x000B8000) != 0x000B8000) {
        return 0;
    }
    if (paging_get_physical(0x50000000) != (uintptr_t) -1) {
        return 0;
    }

    /* 2. Arbitrary mapping test */
    uintptr_t f1 = pmm_alloc_frame();
    if (f1 == 0) return 0;

    uintptr_t test_vaddr = 0x60000000;
    if (paging_map_page(test_vaddr, f1, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        pmm_free_frame(f1);
        return 0;
    }
    if (paging_get_physical(test_vaddr) != f1) {
        paging_unmap_page(test_vaddr);
        pmm_free_frame(f1);
        return 0;
    }
    if (paging_get_physical(test_vaddr + 0x456) != (f1 + 0x456)) {
        paging_unmap_page(test_vaddr);
        pmm_free_frame(f1);
        return 0;
    }

    /* 3. Write / read test */
    volatile uint32_t *test_ptr = (volatile uint32_t *) test_vaddr;
    *test_ptr = 0xDEADBEEF;
    if (*test_ptr != 0xDEADBEEF) {
        paging_unmap_page(test_vaddr);
        pmm_free_frame(f1);
        return 0;
    }
    *test_ptr = 0x12345678;
    if (*test_ptr != 0x12345678) {
        paging_unmap_page(test_vaddr);
        pmm_free_frame(f1);
        return 0;
    }

    /* 4. Unmapping test */
    paging_unmap_page(test_vaddr);
    if (paging_get_physical(test_vaddr) != (uintptr_t) -1) {
        pmm_free_frame(f1);
        return 0;
    }
    pmm_free_frame(f1);

    /* 5. Multiple consecutive pages test */
    uintptr_t f_multi = pmm_alloc_frames(3);
    if (f_multi == 0) return 0;

    uintptr_t multi_vaddr = 0x60010000;
    for (int i = 0; i < 3; i++) {
        if (paging_map_page(multi_vaddr + i * PAGE_SIZE, f_multi + i * PAGE_SIZE, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            return 0;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (paging_get_physical(multi_vaddr + i * PAGE_SIZE) != (f_multi + i * PAGE_SIZE)) {
            return 0;
        }
        volatile uint32_t *p = (volatile uint32_t *) (multi_vaddr + i * PAGE_SIZE);
        *p = 0xA0B0C0D0 + (uint32_t) i;
    }
    for (int i = 0; i < 3; i++) {
        volatile uint32_t *p = (volatile uint32_t *) (multi_vaddr + i * PAGE_SIZE);
        if (*p != 0xA0B0C0D0 + (uint32_t) i) {
            return 0;
        }
        paging_unmap_page(multi_vaddr + i * PAGE_SIZE);
        if (paging_get_physical(multi_vaddr + i * PAGE_SIZE) != (uintptr_t) -1) {
            return 0;
        }
    }
    pmm_free_range(f_multi, 3);

    /* 6. Page-table allocation through PMM test */
    page_directory_t *pd = (page_directory_t *) kernel_pd_phys;
    uint32_t empty_pde = 0;
    for (uint32_t i = 512; i < 1024; i++) {
        if (!(pd->entries[i] & PAGE_PRESENT)) {
            empty_pde = i;
            break;
        }
    }
    if (empty_pde == 0) return 0;

    uintptr_t pt_vaddr = empty_pde << 22;
    size_t used_before = pmm_used_frames();
    uintptr_t f_pt_test = pmm_alloc_frame();
    if (f_pt_test == 0) return 0;

    if (paging_map_page(pt_vaddr, f_pt_test, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        pmm_free_frame(f_pt_test);
        return 0;
    }
    if (pmm_used_frames() != used_before + 2) {
        paging_unmap_page(pt_vaddr);
        pmm_free_frame(f_pt_test);
        return 0;
    }

    uintptr_t f_pt_test2 = pmm_alloc_frame();
    if (f_pt_test2 == 0) return 0;
    if (paging_map_page(pt_vaddr + PAGE_SIZE, f_pt_test2, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        paging_unmap_page(pt_vaddr);
        pmm_free_frame(f_pt_test);
        pmm_free_frame(f_pt_test2);
        return 0;
    }
    if (pmm_used_frames() != used_before + 3) {
        return 0;
    }
    paging_unmap_page(pt_vaddr);
    paging_unmap_page(pt_vaddr + PAGE_SIZE);
    pmm_free_frame(f_pt_test);
    pmm_free_frame(f_pt_test2);

    /* Clean up allocated test page table and restore empty PDE */
    uintptr_t test_pt_phys = pd->entries[empty_pde] & PAGE_FRAME_MASK;
    pd->entries[empty_pde] = 0;
    pmm_free_frame(test_pt_phys);

    /* 7. TLB invalidation test */
    uintptr_t f_tlb1 = pmm_alloc_frame();
    uintptr_t f_tlb2 = pmm_alloc_frame();
    if (!f_tlb1 || !f_tlb2) return 0;

    uintptr_t tlb_vaddr = 0x60020000;
    if (paging_map_page(tlb_vaddr, f_tlb1, PAGE_PRESENT | PAGE_WRITABLE) != 0) return 0;
    volatile uint32_t *tlb_ptr = (volatile uint32_t *) tlb_vaddr;
    *tlb_ptr = 0x11223344;
    if (*tlb_ptr != 0x11223344) return 0;

    paging_unmap_page(tlb_vaddr);

    if (paging_map_page(tlb_vaddr, f_tlb2, PAGE_PRESENT | PAGE_WRITABLE) != 0) return 0;
    *tlb_ptr = 0x55667788;
    if (*tlb_ptr != 0x55667788) return 0;

    paging_unmap_page(tlb_vaddr);
    pmm_free_frame(f_tlb1);
    pmm_free_frame(f_tlb2);

    return 1;
}
