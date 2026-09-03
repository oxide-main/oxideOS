AS = nasm
CC = gcc
LD = ld

ASFLAGS = -f elf32
CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-stack-protector -fno-builtin -Ikernel
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

C_SRC   = $(shell find kernel -type f -name '*.c')
ASM_SRC = $(shell find boot kernel -type f \( -name '*.s' -o -name '*.asm' \))

C_OBJ   = $(C_SRC:.c=.o)
ASM_OBJ = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(ASM_SRC)))

OBJS = $(ASM_OBJ) $(C_OBJ)

ISO_ROOT = iso
KERNEL_BIN = $(ISO_ROOT)/boot/oxideOS.bin
ISO_OUT = oxideOS.iso

LIMINE_DIR = external/limine
LIMINE_BRANCH = v11.x-binary
LIMINE = $(LIMINE_DIR)/limine
LIMINE_RUN = /tmp/oxideos-limine
LIMINE_STAMP = $(LIMINE_DIR)/.iso-files-copied
LIMINE_BOOT_FILES = \
	$(ISO_ROOT)/boot/limine/limine-bios.sys \
	$(ISO_ROOT)/boot/limine/limine-bios-cd.bin \
	$(ISO_ROOT)/boot/limine/limine-uefi-cd.bin \
	$(ISO_ROOT)/EFI/BOOT/BOOTIA32.EFI \
	$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
LIMINE_SRC_BOOT_FILES = \
	$(LIMINE_DIR)/limine-bios.sys \
	$(LIMINE_DIR)/limine-bios-cd.bin \
	$(LIMINE_DIR)/limine-uefi-cd.bin \
	$(LIMINE_DIR)/BOOTIA32.EFI \
	$(LIMINE_DIR)/BOOTX64.EFI

.PHONY: all clean run iso limine-fetch

all: $(ISO_OUT)

%.o: %.s
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(OBJS)
	mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

limine-fetch:
	@if [ ! -d "$(LIMINE_DIR)" ]; then \
		git clone --depth 1 --branch $(LIMINE_BRANCH) https://github.com/Limine-Bootloader/Limine.git $(LIMINE_DIR); \
	fi
	$(MAKE) -C $(LIMINE_DIR)

$(LIMINE_STAMP): limine-fetch $(LIMINE_SRC_BOOT_FILES)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTIA32.EFI $(ISO_ROOT)/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	touch $(LIMINE_STAMP)

$(ISO_OUT): $(KERNEL_BIN) $(LIMINE) $(LIMINE_STAMP) $(ISO_ROOT)/boot/limine/limine.conf $(ISO_ROOT)/boot/limine/limine-background.jpg
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO_OUT)
	cp $(LIMINE) $(LIMINE_RUN)
	chmod +x $(LIMINE_RUN)
	$(LIMINE_RUN) bios-install $(ISO_OUT)

run: $(ISO_OUT)
	qemu-system-i386 -cdrom $(ISO_OUT)

clean:
	rm -f $(OBJS) $(KERNEL_BIN) $(ISO_OUT)
	rm -f $(LIMINE_BOOT_FILES) $(LIMINE_STAMP)
