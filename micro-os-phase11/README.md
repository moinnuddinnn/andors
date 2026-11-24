# Micro-OS v1.1 — Two native OS-service operations + reliable queued IPC

**Target:** seL4 Microkit 2.4.0 · QEMU AArch64 (`qemu_virt_aarch64`) · C.

Building on the user's working v1.0 release, v1.1 demonstrates that an
allowlisted C-compiled native function can issue **two separate service requests**
through a callback to another protection domain:

1. `service(1, x)` → independent console service prints `x` in decimal.
2. `service(2, x)` → independent console service prints `x` in hexadecimal.

The console channel now has an 8-entry single-producer/single-consumer queue.
This avoids overwriting the earlier message when notifications coalesce. The
reader acknowledges consumed entries. If full, the producer rejects the new
request rather than overwriting old data. Queue operations use acquire/release
atomics and have one producer and one consumer only.

**Still limited:** These are approved callbacks executed inside one
preconfigured Microkit protection domain, **not** dynamic processes, general
syscalls, arbitrary C programs, or a production-grade application runtime.

## Install and run on Ubuntu

```bash
cd ~/andros
unzip ~/Downloads/micro-os-phase11.zip
cd micro-os-phase11
# Do this once, BEFORE creating files in v1.1. Never copy on each restart!
cp ../micro-os-phase10/disk.img ./disk.img
make check
make test
make test-compiler
make clean
make
make run
```

`disk.img` is kept across `make clean` and `make run` and retains the same
on-disk MicroFS checkpoint format as v1.0. It is best to make a separate backup
of the original v1.0 disk before starting.

## Generate a native C program on Ubuntu

Open a second host terminal:

```bash
cd ~/andros/micro-os-phase11
python3 tools/make_multi_elf.py examples/multi.c --value 42
```

The generator validates **exact Clang -Oz AArch64 instruction bytes** and prints
one line beginning `write /apps/multi.elf ...`. Paste that entire line into the
**Micro-OS guest** terminal, not the Ubuntu host terminal.

The same full write command is in `examples/multi_commands.txt`. A sample
binary `examples/multi.elf` is also bundled for inspection with `readelf`.

If `/apps` does not exist yet, run `mkdir /apps` inside Micro-OS.

Then inside **Micro-OS**:

```
ls /apps
elfrun /apps/multi.elf 42
elfrun /apps/multi.elf 255
sync
```

Expected **for 42** (the two console messages can interleave with shell text
because the service is asynchronous):

```
[CONSOLE] decimal: 42
[CONSOLE] hexadecimal: 0x0000002A
[ABI] queued decimal and hex requests for 42
native C returned 42 (two AArch64 ABI calls)
```

Expected for `255` includes hexadecimal `0x000000FF`. The generator's `--value`
flag only selects the demonstration input; the binary works with any value in
our existing `elfrun` range of `0..10000`.

After `sync`, exit QEMU with Ctrl+A then X, run `make run` again, and execute
`elfrun /apps/multi.elf 42` to demonstrate persistent program storage.

**Backwards compatibility:** `return42.elf`, `square.elf`, `store.elf`, and
`log.elf` continue to validate and execute. The console diagnostic for the old
`log.elf` now uses the uniform `[CONSOLE] decimal:` format. Disk format remains
unchanged; no reformatting is needed.

## Implementation

- Ten static protection domains, unchanged from v1.0.
- Shared console queue at `0x040c0000`; only native runner and console server map
  it. The console server's mapping changes from R to RW so it can acknowledge
  requests; neither maps the UART's MMIO.
- Operation 1: `CONSOLE_OP_PRINT_DEC`; operation 2: `CONSOLE_OP_PRINT_HEX`.
- `native_runner` accepts new ELF entry offset **124**, code size **60 bytes**;
  caller receives `x` in `w0` and the approved function pointer in `x1`.
- The serialized ELF is **184 bytes** (**248 Base64 characters**), small enough
  for MicroFS's existing 255-byte maximum file content.
- A strict instruction allowlist rejects modified machine code, relocations,
  unsupported ELF layouts, and unexpected stack/control-flow instructions.
- `-global virtio-mmio.force-legacy=false` is retained for the tested modern
  VirtIO-MMIO implementation under QEMU.

## Testing scope

`make test` checks the actual filesystem, device-driver, application, VM,
ELF, and console-service code in host-side test harnesses. It explicitly
verifies **two enqueues before one notification**, no duplicate delivery,
queue wraparound, overflow rejection, and valid/invalid ELF patterns.

`make test-compiler` verifies that Clang produces exactly the allowlisted
instruction patterns for the v0.9, v1.0, and v1.1 samples.

These tests **do not** replace a full seL4 Microkit build/boot. The project
has not been boot-tested by the author of this package in QEMU. Your Ubuntu
Microkit environment provides that final integration test.

See `docs/phase11-architecture.md` for the service protocol and constraints.
