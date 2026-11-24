# Micro-OS v1.2 — Native C bundles and supervised application faults

Target: seL4 Microkit 2.4.0, QEMU AArch64, Ubuntu host. This is a research prototype, not a production OS or a sandbox for untrusted programs.

## Install and boot

Stop QEMU in the old project before copying its disk. If v1.1 was not tested, use the last known-good v1.0 disk instead. Copy once only, never after you have written new files in v1.2.

```sh
sudo apt install clang lld
cd ~/andros
unzip ~/Downloads/micro-os-phase12.zip
cd micro-os-phase12
cp -n ../micro-os-phase11/disk.img ./disk.img
make check
make test
make test-compiler
make
make run
```

If no disk exists, `make run` creates an empty one. The MicroFS disk layout and QEMU modern VirtIO settings are unchanged. Earlier commands and the restricted `elfrun` ABI remain supported. `make clean` does not remove `disk.img`.

## Run a real C program with loops

In a second Ubuntu terminal, run:

```sh
cd ~/andros/micro-os-phase12
python3 tools/make_trusted_elf.py examples/series.c --value 9
```

Or open `examples/series_commands.txt` (generated and included). In Micro-OS, ensure `/apps` exists. Paste each generated `write` command separately and wait for the next prompt before pasting the next. Do not paste the complete batch: the shell still discards typeahead during an outstanding operation. The manifest is written last. Then:

```text
nativeexec /apps/series.elf 9
```

Expected: `trusted C returned 285; scratch[0]=285`. Test 12 (650), then `sync`, quit QEMU via Ctrl+A then X, and reboot with `make run`. Run the command again.

## Fault containment experiment

Save the filesystem before this test. Use `examples/fault_commands.txt`, or generate commands with `python3 tools/make_trusted_elf.py examples/fault.c --value 0`. Paste each line separately. `nativeexec /apps/fault.elf 0` executes a deliberate undefined AArch64 instruction. Expected: `trusted C fault; runner quarantined (label ...)`. `ps` should show `trusted_runner FAULTED`. `run calc 7 + 5`, `ls /apps`, and old `elfrun` should still work. A subsequent trusted job is rejected until QEMU is restarted. No automatic restart is attempted: Microkit restart changes a program counter, not all compromised state. Fault type/label is reported as a number.

## Architecture / ABI

The new worker is a child PD of `app_manager` (ID 1). It has no direct hardware or filesystem mapping. The terminal reassembles a manifest (`MOS12 <byte_count> <CRC32 decimal>`) and numbered Base64 chunk files (up to 180 decoded bytes per chunk), verifies CRC and ELF structure, copies to a 4 KiB shared code page, performs cache maintenance, and dispatches to the manager. The worker maps this page RX, terminal maps it RW, and the worker independently checks the image header. Shared code is not globally immutable; the shell is trusted. No claim of global W^X or security against malicious native code.

The ABI is `uint32_t program(uint32_t input, uint32_t *scratch)`, with 1024 zero-initialized uint32_t elements (4 KiB private data), an ordinary runner stack, and an integer return value. Programs must be trusted, finite, position-independent freestanding functions. The packer links C with Clang/LLD, disallows writable globals/GOT and unresolved relocations, supports internal functions and read-only data within one RX segment, and repackages at fixed entry offset 128. ELF64 ET_DYN, little-endian AArch64, exactly one RX PT_LOAD, 132–2048 bytes. This is not a Linux ELF loader, libc, native syscall ABI, or new process creation. No watchdog; an infinite loop may leave the shell waiting until reboot. Malicious code can corrupt worker memory/protocol state; do not run third-party binaries. The CRC is an integrity check, not authentication.

MicroFS retains its 48-node and 255-byte-per-file limits. A 2048-byte bundle takes 12 chunk files + manifest, so check `fsinfo` before importing. To delete a bundle, remove the manifest and numbered chunk files separately. A partially written bundle fails validation; stale extra chunks are ignored and still occupy entries. Only `sync` persists changes.

## Tests

`make test` runs seven host suites, including real FS code with simulated disk, application-manager fault injection, parser/CRC tests and simulated IPC. `make test-compiler` checks the older compiler patterns. All 13 C translation units were cross-compiled to AArch64 objects using Clang and a test Microkit header. XML mapping symbols and channel IDs checked against these objects. The actual Microkit tool, seL4 runtime, real exception delivery, and QEMU execution have NOT been tested in the packaging environment. Verify on your Ubuntu machine.

Reference: https://docs.sel4.systems/projects/microkit/manual/2.4.0/ (child PD faults, mappings, IPC).
