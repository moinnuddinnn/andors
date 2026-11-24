# Micro-OS v1.0 — Native C-to-OS Service ABI Prototype

Built on the working v0.9 seL4 Microkit 2.4.0 / QEMU AArch64 project.

## Milestone

The native runner executes a Clang-compiled C function that calls a **restricted
callback** into the runner runtime. That runtime notifies a **separate console
service protection domain** with a number to log. The console server writes to
Microkit debug output. This is **not** a general syscall ABI or a dynamic ELF loader.

Ten PDs: UART, VirtIO-block, filesystem, terminal, app manager, calculator,
counter, script VM, native runner, and **console_service**.

## Build (Ubuntu with the SDK already installed)

```bash
cd ~/andros
unzip ~/Downloads/micro-os-phase10.zip
cd micro-os-phase10
# Copy disk.img from v0.9 only ONCE, before adding v1.0 files:
cp ../micro-os-phase9/disk.img ./disk.img
make check
make test
make test-compiler
make clean
make
make run
```

Keep disk.img across rebuilds. `make clean` only removes build/, not disk.img.

## Compile native C sample on Ubuntu (second terminal)

```bash
cd ~/andros/micro-os-phase10
python3 tools/make_abi_elf.py examples/log.c --value 9
```

Paste the complete `write /apps/log.elf ...` command into the Micro-OS guest.
If `/apps` doesn't exist, create it first with `mkdir /apps`.
The full command is also in `examples/log_commands.txt`.

Inside **Micro-OS**:

```
ls /apps
elfrun /apps/log.elf 9
elfrun /apps/log.elf 12
sync
```

Expected lines on QEMU (the console server prints through Microkit debug output):

```
[CONSOLE] native app printed: 9
[ABI] print_u32(9) sent to console service
native C returned 88 (AArch64 ABI callback)
[CONSOLE] native app printed: 12
[ABI] print_u32(12) sent to console service
native C returned 151 (AArch64 ABI callback)
```

Restart QEMU, retaining the same `disk.img`, then run `elfrun /apps/log.elf 9`.

## How it works

- ELF file: exactly **184 binary bytes**, **248 Base64 characters**, within
  MicroFS's 255-byte per-file limit.
- Entry point: offset **136**. Exactly **40 bytes** of validated instructions.
- The AArch64 function uses stack save/restore and calls the runtime callback.
- Callback writes one integer into a shared page and notifies console_service.
- Console server does **not** map PL011 MMIO. It prints with `microkit_dbg_puts`.
- Native runner returns the calculation to the shell through app manager IPC.
- Older `return42.elf`, `square.elf`, and `store.elf` remain supported.

## Caveats

- Real Microkit/QEMU boot has not been tested in this build environment.
- Running the native sample is intended only for QEMU AArch64 on the tested
  Microkit 2.4.0 board; cross-compiler version may change Clang instruction
  generation. The generator rejects anything outside the exact allowlist.
- Static PDs; not dynamic processes, conventional syscalls, or arbitrary ELF.
- Only one outstanding native console message per call; no retries or delivery
  guarantee; Microkit notifications can coalesce.
- This is a learning prototype, not production-grade isolation or driver code.
