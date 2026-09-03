#include <stdint.h>
#include "acpi.h"
#include "common_headers/io.h"

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_RSDT_SIGNATURE "RSDT"
#define ACPI_XSDT_SIGNATURE "XSDT"
#define ACPI_FADT_SIGNATURE "FACP"
#define ACPI_DSDT_SIGNATURE "DSDT"

#define ACPI_PM1_CNT_SCI_EN (1u << 0)
#define ACPI_PM1_CNT_SLP_TYP_MASK (7u << 10)
#define ACPI_PM1_CNT_SLP_EN (1u << 13)

#define ACPI_PM1_CNT_PRESERVE_MASK \
    ((1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | \
     (1u << 7) | (1u << 8) | (1u << 9) | (1u << 14) | (1u << 15))

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

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
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
} acpi_gas_t;

typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t int_model;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved;
    uint32_t flags;
    acpi_gas_t reset_reg;
    uint8_t reset_value;
    uint16_t arm_boot_arch;
    uint8_t fadt_minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    acpi_gas_t x_pm1a_evt_blk;
    acpi_gas_t x_pm1b_evt_blk;
    acpi_gas_t x_pm1a_cnt_blk;
    acpi_gas_t x_pm1b_cnt_blk;
} acpi_fadt_t;

static const acpi_fadt_t* fadt;
static uint16_t pm1a_cnt;
static uint16_t pm1b_cnt;
static uint8_t slp_typ_a;
static uint8_t slp_typ_b;
static int acpi_ready;
static acpi_power_status_t init_status = ACPI_POWER_NO_RSDP;

static int memeq(const char* a, const char* b, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static uint8_t checksum(const void* ptr, uint32_t len)
{
    const uint8_t* bytes = (const uint8_t*) ptr;
    uint8_t sum = 0;

    for (uint32_t i = 0; i < len; i++) {
        sum = (uint8_t) (sum + bytes[i]);
    }

    return sum;
}

static uint16_t read_phys_u16(uint32_t addr)
{
    uint16_t value;

    __asm__ volatile ("movw (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

static const acpi_rsdp_t* find_rsdp_range(uint32_t start, uint32_t end)
{
    for (uint32_t addr = start; addr < end; addr += 16) {
        const acpi_rsdp_t* rsdp = (const acpi_rsdp_t*) addr;

        if (memeq(rsdp->signature, ACPI_RSDP_SIGNATURE, 8) &&
            checksum(rsdp, 20) == 0) {
            return rsdp;
        }
    }

    return 0;
}

static const acpi_rsdp_t* find_rsdp(void)
{
    uint16_t ebda_segment = read_phys_u16(0x40E);
    uint32_t ebda = ((uint32_t) ebda_segment) << 4;
    const acpi_rsdp_t* rsdp = 0;

    if (ebda != 0) {
        rsdp = find_rsdp_range(ebda, ebda + 1024);
        if (rsdp != 0) {
            return rsdp;
        }
    }

    return find_rsdp_range(0xE0000, 0x100000);
}

static int valid_sdt(const acpi_sdt_header_t* header, const char* signature)
{
    if (header == 0 || !memeq(header->signature, signature, 4)) {
        return 0;
    }

    if (header->length < sizeof(acpi_sdt_header_t)) {
        return 0;
    }

    return checksum(header, header->length) == 0;
}

static const acpi_sdt_header_t* find_table_rsdt(
    const acpi_sdt_header_t* rsdt,
    const char* signature
)
{
    const uint32_t* entries = (const uint32_t*) (rsdt + 1);
    uint32_t count = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;

    for (uint32_t i = 0; i < count; i++) {
        const acpi_sdt_header_t* table = (const acpi_sdt_header_t*) entries[i];

        if (valid_sdt(table, signature)) {
            return table;
        }
    }

    return 0;
}

static const acpi_sdt_header_t* find_table_xsdt(
    const acpi_sdt_header_t* xsdt,
    const char* signature
)
{
    const uint64_t* entries = (const uint64_t*) (xsdt + 1);
    uint32_t count = (xsdt->length - sizeof(acpi_sdt_header_t)) / 8;

    for (uint32_t i = 0; i < count; i++) {
        if ((entries[i] >> 32) != 0) {
            continue;
        }

        const acpi_sdt_header_t* table = (const acpi_sdt_header_t*) (uint32_t) entries[i];
        if (valid_sdt(table, signature)) {
            return table;
        }
    }

    return 0;
}

static const acpi_sdt_header_t* find_table(
    const acpi_rsdp_t* rsdp,
    const char* signature
)
{
    if (rsdp->revision >= 2 && rsdp->xsdt_addr != 0 &&
        (rsdp->xsdt_addr >> 32) == 0) {
        const acpi_sdt_header_t* xsdt =
            (const acpi_sdt_header_t*) (uint32_t) rsdp->xsdt_addr;

        if (valid_sdt(xsdt, ACPI_XSDT_SIGNATURE)) {
            const acpi_sdt_header_t* table = find_table_xsdt(xsdt, signature);
            if (table != 0) {
                return table;
            }
        }
    }

    if (rsdp->rsdt_addr != 0) {
        const acpi_sdt_header_t* rsdt = (const acpi_sdt_header_t*) rsdp->rsdt_addr;

        if (valid_sdt(rsdt, ACPI_RSDT_SIGNATURE)) {
            return find_table_rsdt(rsdt, signature);
        }
    }

    return 0;
}

static uint32_t aml_pkg_length_size(const uint8_t* aml)
{
    return ((*aml >> 6) & 0x3) + 1;
}

static int aml_read_integer(const uint8_t** cursor, const uint8_t* end, uint8_t* value)
{
    const uint8_t* p = *cursor;

    if (p >= end) {
        return 0;
    }

    if (*p == 0x00) {
        *value = 0;
        *cursor = p + 1;
        return 1;
    }

    if (*p == 0x01) {
        *value = 1;
        *cursor = p + 1;
        return 1;
    }

    if (*p == 0x0A && p + 1 < end) {
        *value = p[1];
        *cursor = p + 2;
        return 1;
    }

    if (*p == 0x0B && p + 2 < end) {
        *value = p[1];
        *cursor = p + 3;
        return 1;
    }

    if (*p == 0x0C && p + 4 < end) {
        *value = p[1];
        *cursor = p + 5;
        return 1;
    }

    return 0;
}

static acpi_power_status_t parse_s5(const acpi_sdt_header_t* dsdt)
{
    const uint8_t* aml = (const uint8_t*) (dsdt + 1);
    const uint8_t* end = ((const uint8_t*) dsdt) + dsdt->length;

    while (aml + 7 < end) {
        const uint8_t* name = aml + 1;

        if (aml[0] == 0x08 && name < end && *name == 0x5C) {
            name++;
        }

        if (aml[0] == 0x08 && name + 5 < end &&
            memeq((const char*) name, "_S5_", 4) && name[4] == 0x12) {
            const uint8_t* pkg = &name[5];
            uint32_t len_size;
            uint8_t elements;
            uint8_t typ_a;
            uint8_t typ_b;

            len_size = aml_pkg_length_size(pkg);
            if (pkg + len_size >= end) {
                return ACPI_POWER_BAD_S5;
            }

            pkg += len_size;
            elements = *pkg++;
            if (elements < 1) {
                return ACPI_POWER_BAD_S5;
            }

            if (!aml_read_integer(&pkg, end, &typ_a)) {
                return ACPI_POWER_BAD_S5;
            }

            if (elements > 1) {
                if (!aml_read_integer(&pkg, end, &typ_b)) {
                    return ACPI_POWER_BAD_S5;
                }
            } else {
                typ_b = typ_a >> 8;
            }

            if (typ_a > 7 || typ_b > 7) {
                return ACPI_POWER_BAD_S5;
            }

            slp_typ_a = typ_a;
            slp_typ_b = typ_b;
            return ACPI_POWER_OK;
        }

        aml++;
    }

    return ACPI_POWER_NO_S5;
}

void acpi_init(void)
{
    const acpi_rsdp_t* rsdp = find_rsdp();
    const acpi_sdt_header_t* fadt_header;
    const acpi_sdt_header_t* dsdt;
    uint64_t dsdt_addr;

    acpi_ready = 0;
    fadt = 0;

    if (rsdp == 0) {
        init_status = ACPI_POWER_NO_RSDP;
        return;
    }

    if (rsdp->revision >= 2 && rsdp->length >= sizeof(acpi_rsdp_t) &&
        checksum(rsdp, rsdp->length) != 0) {
        init_status = ACPI_POWER_BAD_RSDP;
        return;
    }

    fadt_header = find_table(rsdp, ACPI_FADT_SIGNATURE);
    if (fadt_header == 0) {
        init_status = ACPI_POWER_NO_FADT;
        return;
    }

    if (fadt_header->length < 116) {
        init_status = ACPI_POWER_BAD_FADT;
        return;
    }

    fadt = (const acpi_fadt_t*) fadt_header;
    pm1a_cnt = (uint16_t) fadt->pm1a_cnt_blk;
    pm1b_cnt = (uint16_t) fadt->pm1b_cnt_blk;

    if (pm1a_cnt == 0 || fadt->pm1_cnt_len < 2) {
        init_status = ACPI_POWER_BAD_FADT;
        return;
    }

    dsdt_addr = fadt->dsdt;
    if (fadt_header->length >= 148 && fadt->x_dsdt != 0) {
        dsdt_addr = fadt->x_dsdt;
    }

    if ((dsdt_addr >> 32) != 0) {
        init_status = ACPI_POWER_UNSUPPORTED;
        return;
    }

    dsdt = (const acpi_sdt_header_t*) (uint32_t) dsdt_addr;
    if (!valid_sdt(dsdt, ACPI_DSDT_SIGNATURE)) {
        init_status = ACPI_POWER_NO_DSDT;
        return;
    }

    init_status = parse_s5(dsdt);
    acpi_ready = init_status == ACPI_POWER_OK;
}

acpi_power_status_t acpi_poweroff(void)
{
    uint16_t pm1a;
    uint16_t pm1b;

    if (!acpi_ready) {
        return init_status;
    }

    if ((inw(pm1a_cnt) & ACPI_PM1_CNT_SCI_EN) == 0 &&
        fadt->smi_cmd != 0 && fadt->acpi_enable != 0) {
        outb((uint16_t) fadt->smi_cmd, fadt->acpi_enable);

        for (uint32_t i = 0; i < 1000000; i++) {
            if ((inw(pm1a_cnt) & ACPI_PM1_CNT_SCI_EN) != 0) {
                break;
            }
            io_wait();
        }
    }

    pm1a = inw(pm1a_cnt) & ACPI_PM1_CNT_PRESERVE_MASK;
    pm1b = pm1b_cnt != 0 ? (inw(pm1b_cnt) & ACPI_PM1_CNT_PRESERVE_MASK) : pm1a;

    pm1a |= ((uint16_t) slp_typ_a) << 10;
    pm1b |= ((uint16_t) slp_typ_b) << 10;

    __asm__ volatile ("cli");

    outw(pm1a_cnt, pm1a);
    if (pm1b_cnt != 0) {
        outw(pm1b_cnt, pm1b);
    }

    pm1a |= ACPI_PM1_CNT_SLP_EN;
    pm1b |= ACPI_PM1_CNT_SLP_EN;

    outw(pm1a_cnt, pm1a);
    if (pm1b_cnt != 0) {
        outw(pm1b_cnt, pm1b);
    }

    return ACPI_POWER_OK;
}

const char* acpi_power_status_string(acpi_power_status_t status)
{
    switch (status) {
    case ACPI_POWER_OK:
        return "ok";
    case ACPI_POWER_NO_RSDP:
        return "RSDP not found";
    case ACPI_POWER_BAD_RSDP:
        return "RSDP checksum failed";
    case ACPI_POWER_NO_FADT:
        return "FADT not found";
    case ACPI_POWER_BAD_FADT:
        return "FADT is missing PM1 control registers";
    case ACPI_POWER_NO_DSDT:
        return "DSDT not found or invalid";
    case ACPI_POWER_NO_S5:
        return "_S5_ package not found";
    case ACPI_POWER_BAD_S5:
        return "_S5_ package is invalid";
    case ACPI_POWER_UNSUPPORTED:
        return "ACPI table address is above 4 GiB";
    default:
        return "unknown ACPI error";
    }
}
