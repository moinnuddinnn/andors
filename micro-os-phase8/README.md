# Micro-OS v0.8 — Compile C into a restricted AArch64 ELF and run it

**Built on the v0.7 project that successfully ran `return42.elf` on seL4 Microkit 2.4.0/QEMU AArch64.**

This release preserves nine preconfigured protection domains, PL011 UART,
MicroFS persistent storage with two checkpoints, VirtIO-MMIO with
`-global virtio-mmio.force-legacy=false`, IPC, calculator, counter and `.mos`
script interpreter.

## The new milestone

The host tool **really compiles C** using Clang (target AArch64), extracts the
machine instructions from its `.text` section, verifies a tiny allowlisted
function, and wraps the instructions in a **160-byte ELF64** file. Its 216-byte
Base64 representation fits MicroFS's 255-byte per-file storage limit. The
shell's input and filesystem mailbox were expanded to support this size.

This is the supported example:

```c
#include <stdint.h>
uint32_t program(uint32_t x) { return x*x + 7u; }
```

The loader **only accepts** compiled code matching three instructions:
`movz wN, #K; madd w0,w0,w0,wN; ret` (Clang -Os), plus the legacy v0.7
`movz w0, #K; ret`. Other native instructions, larger ELF files, multiple
segments, relocations, dynamic linking, syscalls, libc, BSS, stack-dependent
functions, arbitrary C programs and dynamic process creation are **not**
supported. Inputs are restricted to 0..10000. This is a single predefined
runner PD—not a new process per invocation. **Never use this loader for
untrusted code.**

## 1. Setup on Ubuntu

```bash
sudo apt install clang            # only needed for compiling sample C programs
cd ~/andros
unzip ~/Downloads/micro-os-phase8.zip
cd micro-os-phase8
```

Preserve your disk from v0.7, once, **before** creating files in v0.8:

```bash
cp ../micro-os-phase7/disk.img ../micro-os-phase7/disk.img.backup
cp ../micro-os-phase7/disk.img ./disk.img
```

**Do not repeat this copy after saving v0.8 data!** `make clean` never removes
`disk.img`. `make reset-disk` WILL erase it.

## 2. Build and test

```bash
make check
make test
make clean
make
make run
```

To exit QEMU use Ctrl+A, then X.

## 3. Compile C on Ubuntu, paste into Micro-OS

In a separate Ubuntu terminal, from `~/andros/micro-os-phase8`:

```bash
python3 tools/make_c_elf.py examples/square.c --value 9
```

The tool prints a `write /apps/square.elf <216-character Base64 payload>`
command and an `elfrun /apps/square.elf 9` command. **Copy the entire `write`
command into the running Micro-OS console**, not Ubuntu. First create `/apps`
if you haven't already (`mkdir /apps`; if it already exists, skip). You can
instead copy the ready-made write command from `examples/square_commands.txt`.

Inside Micro-OS:

```text
micro-os> mkdir /apps
micro-os> write /apps/square.elf <paste the complete 216-character string>
wrote 216 bytes
micro-os> ls /apps
micro-os> elfrun /apps/square.elf 9
native ELF returned 88 (AArch64 machine code)
micro-os> elfrun /apps/square.elf 12
native ELF returned 151 (AArch64 machine code)
micro-os> elfrun /apps/return42.elf
native ELF returned 42 (AArch64 machine code)
micro-os> sync
Filesystem saved successfully ...
```

**Note:** the `write ... <paste...>` line above is a placeholder; do not type the
angle brackets. Use the exact output of the generator.

Restart QEMU, and try `elfrun /apps/square.elf 9` again to verify persistence.

You can edit `examples/square.c` to change the constant `7` (0..65535),
then regenerate. More substantial C changes will be rejected until we build
a proper multi-segment ELF loader and a process ABI.

## Why the protection domains remain important

- The terminal reads the ELF as text from the private filesystem service.
- The **terminal maps the executable page RW, not executable** and copies
  only a validated instruction sequence, followed by D-cache maintenance.
- The precreated native runner maps the same page **RX, not writable**,
  invalidates the I-cache where needed, and executes the function with the
  argument passed in AArch64 register `w0`.
- The result travels back via the application manager IPC mailbox.
- The runner has no device MMIO mappings.

This is a research prototype: protocol sequencing is single-flight and
there is no robust crash containment, supervisor restart or timeout if a
protection domain faults. The instruction allowlist reduces, but does not
eliminate, the need for further security review.

## Development tests

`make test` covers filesystem snapshots, VirtIO descriptors, all application
IPC, scripting, ELF validation, and a 216-byte ELF text file surviving a
simulated restart. Tests run with host stubs; they do not prove real seL4/QEMU
execution. Your local `make run` is the authoritative integration check.
