#ifndef ACPI_H
#define ACPI_H

typedef enum {
    ACPI_POWER_OK = 0,
    ACPI_POWER_NO_RSDP,
    ACPI_POWER_BAD_RSDP,
    ACPI_POWER_NO_FADT,
    ACPI_POWER_BAD_FADT,
    ACPI_POWER_NO_DSDT,
    ACPI_POWER_NO_S5,
    ACPI_POWER_BAD_S5,
    ACPI_POWER_UNSUPPORTED,
} acpi_power_status_t;

void acpi_init(void);
acpi_power_status_t acpi_poweroff(void);
const char* acpi_power_status_string(acpi_power_status_t status);

#endif
