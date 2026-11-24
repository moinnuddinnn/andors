# Micro-OS changelog

## v1.1 — two native console operations with bounded queued IPC

- New Clang-compiled `examples/multi.c`: calls a runner-provided native ABI
  service twice (`print_decimal(x)` and `print_hex(x)`) and returns `x`.
- New `tools/make_multi_elf.py` creates a strictly allowlisted 184-byte ELF64
  for AArch64; entry 124, 60-byte instruction sequence, 248-character Base64.
- Replaced console's single slot with an 8-entry SPSC ring carrying operation
  ID and number; acquire/release index publication and consumer acknowledgement.
- Console service emits decimal or uppercase 8-digit hexadecimal output.
- Retained existing v1.0 numerical log ABI and previous native examples.
- Updated console-service mapping to RW **only** to acknowledge the ring.
- Extended ELF, application-IPC, and console tests to cover the new mode.
- All previous filesystem and persistent disk storage functionality retained.

## v1.0 — restricted native C-to-console-service ABI

Added independent console-service PD and a single-log callback interface.

## v0.9 and earlier

The earlier stages established the microkernel boot, UART driver, isolated
shell, RAM and persistent MicroFS, application manager, script VM, native ELF
execution and a private writable data page.
