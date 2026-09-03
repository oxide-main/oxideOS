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
