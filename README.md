# oxideOS

oxideOS is a small 32-bit x86 operating system used as an OSDev learning
project. The current codebase boots a freestanding C kernel, initializes core
x86 descriptor and interrupt infrastructure, manages physical and virtual
memory, and starts an interactive VGA text shell with keyboard input.

The current kernel version is `0.2.0`. The boot path, memory managers,
hardware drivers, shell, and boot-time self-tests are implemented. The project
is still a single-kernel hobby OS: it has no filesystem, scheduler, userspace,
or networking yet.

## Current Architecture

* oxideOS is an ELF32/i386 kernel that runs in 32-bit protected mode.
* `boot/boot.s` provides the Multiboot1 header, sets up an early stack, and
  enters `kernel_main`.
* `linker.ld` links the kernel at 1 MiB and keeps the Multiboot header at the
  start of the image.
* `kernel/arch/x86` contains GDT setup, IDT setup, ISR/IRQ dispatch, a bitmap
  physical memory manager, an identity-mapped paging layer, and a kernel heap.
* `kernel/drivers/pic.c` remaps and controls the 8259 PIC.
* `kernel/drivers/pit.c` programs the 8253/8254 PIT (channel 0, mode 2) and
  provides a tick-based millisecond uptime counter.
* `kernel/drivers/vga.c` writes to the VGA text buffer at `0xB8000`.
* `kernel/drivers/ps2kbd.c` handles interrupt-driven PS/2 keyboard input using
  the US QWERTY layout in `kernel/misc/kbd_us_qwerty.h`.
* `kernel/drivers/acpi.c` discovers ACPI tables and powers off through the S5
  sleep state when firmware exposes the required PM1 control registers.
* `kernel/kernel.c` initializes the kernel and runs the `oxideOS>` shell.

Paging is identity-mapped (no higher-half kernel and no per-process address
spaces), and the GDT reserves ring-3 selectors that nothing currently uses.
The kernel intentionally remains in 32-bit protected mode and does not enter
x86-64 long mode.

# oxideOS

oxideOS is a 32-bit x86 hobby operating system with a freestanding C kernel.
The current kernel version is `0.2.0`.

## Current Codebase

* Multiboot1 boot entry through Limine.
* GDT, IDT, ISR/IRQ dispatch, and 8259 PIC support.
* PIT timer with millisecond uptime tracking.
* VGA text output and interrupt-driven PS/2 keyboard input.
* ACPI table discovery and S5 shutdown support.
* Bitmap physical memory manager, identity-mapped paging, and kernel heap.
* Boot-time and runtime memory self-tests.
* Interactive shell with command history and US QWERTY keyboard mapping.

## Build and Run

Install `make`, `nasm`, 32-bit freestanding `gcc`, i386 `ld`, `xorriso`, and
`qemu-system-i386`, then run:

```sh
make
make run
```

Other Make targets:

```sh
make clean
make limine-fetch
```

## Shell Commands

`help`, `clear`, `banner`, `echo`, `color`, `about`, `version`, `uname`,
`cpuinfo`, `meminfo`, `memtest`, `neofetch`, `calc`, `uptime`, `history`,
`keymap`, `keys`, `reboot`, `shutdown`, and `halt`.
The kernel still exposes a Multiboot1 header, and the configuration uses
