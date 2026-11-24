# Changelog

## v1.0 — Native C-to-console-service ABI (experimental)

- Adds isolated `console_service` protection domain (ten PDs in total).
- Adds one-way native runner -> console service IPC via `console_mailbox`.
- Adds `examples/log.c`, generated `log.elf`, and `tools/make_abi_elf.py`.
- Supports strictly allowlisted 40-byte Clang AArch64 function with a 32-byte
  stack frame and single callback `blr x1` targeting the runtime-supplied
  `native_log_u32` function. Code contains no general-purpose IO syscalls.
- Enhances ELF loader to accept exactly 184-byte valid ELF images with a
  carefully checked instruction sequence, preserving v0.7/v0.8/v0.9 cases.
- Keeps prior persistent filesystem format and QEMU modern VirtIO setting.
- Adds host tests for log-service notification sequencing and 248-char ELF
  persistence through reboot simulation.

**Not implemented:** dynamic process creation, general ELF loading,
conventional syscalls, preemptive userspace program scheduling, full printf,
file-service calls from native guest code, and fault isolation of arbitrary
untrusted programs. This is an education-only OS prototype.
