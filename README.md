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

## Memory Management

Memory management is fully wired into boot (`kernel_main` calls these in
order, and halts if the self-tests fail):

* **Physical memory manager** (`kernel/arch/x86/pmm.c`) — a bitmap frame
  allocator built from the Multiboot memory map. It reserves frame 0, the
  kernel image, its own bitmaps, and all Multiboot-provided structures
  (mmap, command line, modules, ELF symbol headers, bootloader name) before
  handing out free frames. Supports single- and multi-frame contiguous
  allocation and freeing.
* **Paging** (`kernel/arch/x86/paging.c`) — identity-maps the first 2 MiB,
  the kernel image, PMM bitmaps, paging structures, Multiboot structures, and
  ACPI tables. New page tables are allocated from the PMM and, once paging is
  active, are edited through a recursive scratch-page mapping rather than by
  touching physical memory directly. Includes a page-fault handler (`#PF`,
  vector 14) that prints a diagnostic panic screen (fault address, EIP, error
  code, cause) and a 7-part self-test suite covering identity mapping,
  arbitrary mapping, read/write, unmapping, multi-page ranges, page-table
  allocation through the PMM, and TLB invalidation.
* **Kernel heap** (`kernel/arch/x86/heap.c`) — a first-fit `kmalloc` /
  `kfree` / `kcalloc` / `krealloc` allocator with block splitting and
  coalescing, backed by pages pulled from the PMM on demand as the heap
  grows.
* **Self-tests** (`kernel/arch/x86/memory_test.c`) — exercises the PMM, heap,
  and paging layers together at boot; a failure halts the kernel before the
  shell starts. The same tests are exposed at runtime via the `memtest`
  shell command.

## Boot Process

oxideOS uses [Limine](https://github.com/limine-bootloader/limine) as its
bootloader. The current boot flow is:

```text
BIOS/UEFI -> Limine -> Multiboot1 -> oxideOS ELF32 kernel
```

The kernel still exposes a Multiboot1 header, and the configuration uses
`protocol: multiboot1` in:

```text
iso/boot/limine/limine.conf
```

The direct Limine protocol was tested, but Limine v11 rejected the current
32-bit ELF kernel because that path expects an x86-64 ELF kernel. oxideOS
therefore remains on the Multiboot1 path by design.

The generated ISO includes copied Limine BIOS and UEFI boot files under
`iso/boot/limine` and `iso/EFI/BOOT`. `xorriso` creates a hybrid ISO, then the
Makefile copies Limine to `/tmp/oxideos-limine`, marks that temporary copy
executable, and runs `bios-install` from there. The Makefile dependencies also
track Limine's installer and source boot files, so changes to those files cause
the ISO and BIOS stages to be rebuilt and reinstalled. This fixes the previous
Limine integrity error and avoids changing the tracked mode of
`external/limine/limine`.

The Limine boot menu uses a background image resized to 1280x853. The original
6000x4000 image caused excessive memory usage under QEMU.

QEMU successfully boots the ELF32/i386 kernel, and GDB verification confirmed
32-bit protected mode (`CR0.PE=1`, `EFER=0`).

## Requirements

Build tools expected on the host:

* `make`
* `nasm`
* `gcc` with 32-bit code generation support
* `ld` with ELF i386 support
* `xorriso`
* `git`
* `qemu-system-i386` to run the ISO

The Makefile fetches Limine into `external/limine` from the upstream
`v11.x-binary` branch when needed. Generated object files, copied Limine boot
files, the kernel image, and the ISO are ignored by `.gitignore`.

## Build

Build the bootable ISO:

```sh
make
```

Clean generated objects, copied Limine boot files, the kernel image, and the
ISO:

```sh
make clean
```

Fetch or refresh the local Limine checkout manually:

```sh
make limine-fetch
```

The main build artifact is:

```text
oxideOS.iso
```

## Run

Run oxideOS in QEMU:

```sh
make run
```

This starts:

```sh
qemu-system-i386 -cdrom oxideOS.iso
```

After boot, the kernel initializes GDT/IDT, the memory subsystem (PMM, heap,
paging), and runs its self-tests before enabling interrupts. It then prints
its banner and opens the shell:

```text
oxideOS>
```

## Testing

Two headless QEMU harnesses drive the shell and read back the VGA text buffer
to verify boot output without a display:

* `test_boot.sh` — boots the ISO with QEMU's monitor on a Unix socket, sends
  a keypress past the Limine menu, and dumps the raw VGA text buffer
  (`0xB8000`) plus CPU registers via `socat`.
* `test_qemu.py` — the same approach in Python: boots the ISO, drives the
  shell over the QEMU monitor by sending individual keystrokes (e.g. `clear`,
  `version`, `uname`, `uptime`), and decodes the resulting VGA buffer.

Both require `qemu-system-i386`; `test_boot.sh` also requires `socat`.

## Shell Commands

The shell accepts keyboard input, supports backspace, and handles unknown
commands with an error message.

Available commands:

* `help` - list commands
* `clear` - clear the VGA text console
* `banner` - reprint the startup banner
* `echo <text>` - print text
* `color [list|<name>]` - list or change the shell theme colour
* `about` - show a short oxideOS description
* `version` - show the kernel version
* `uname` - show the OS name and architecture
* `cpuinfo` - show basic CPU information from CPUID
* `meminfo` - show Multiboot memory info, live PMM frame counts, paging
  status/page-directory address, and kernel heap usage
* `memtest` - run the PMM/heap/paging self-test suite on demand
* `neofetch` - show a colourful system summary
* `calc <a> <op> <b>` - evaluate an integer expression using `+`, `-`, `*`, or `/`
* `uptime` - report PIT-backed uptime in seconds
* `history` - show recently run commands
* `keymap` - show the active keyboard layout
* `keys` - show current modifier and lock-key state
* `reboot` - request a warm reboot through the PS/2 controller
* `shutdown` - request ACPI S5 poweroff, reporting table/discovery errors if
  ACPI shutdown is unavailable, then halt if it returns
* `halt` - stop the CPU

Filesystem-style commands such as `ls`, `cat`, `pwd`, and `cd` are not present
because oxideOS does not have a filesystem layer yet.

## ACPI Status

The current ACPI support is intentionally small. It searches the BIOS ACPI
regions for the RSDP, validates the RSDT/XSDT and FADT checksums, reads the
DSDT, extracts the `_S5_` package, enables ACPI through the FADT SMI command
when required, and writes the PM1 control blocks to request S5 poweroff.

The code follows the same full-hardware S5 sequence used by uACPI, but oxideOS
does not vendor or link uACPI yet. A full uACPI integration needs kernel
services that are not present in this tree yet, including PCI config access,
SystemIO callbacks, and basic synchronization primitives (the heap allocator
and virtual memory mapping this note previously called out as missing are now
implemented — see Memory Management above).

## Credits

The ACPI shutdown flow was informed by [uACPI](https://github.com/uACPI/uACPI),
a portable ACPI implementation. uACPI is available under the [MIT
License](https://github.com/uACPI/uACPI/blob/master/LICENSE).

oxideOS uses [Limine](https://github.com/limine-bootloader/limine) as its
bootloader. Limine is available under the [BSD-2-Clause
License](https://github.com/limine-bootloader/limine#).

The Limine background image was created by [Levent Simsek](https://www.pexels.com/photo/brown-tabby-cat-in-close-up-photography-3617160/)
and is available through Pexels. The same image is also used in the original
Limine bootloader image on the project's website.

Created and maintained by Johan & Pranav.
