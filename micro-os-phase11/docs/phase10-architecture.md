# Micro-OS v1.0: restricted native-to-service ABI

The sample Clang C function `program(uint32_t x, micro_os_log_fn log_number)`
uses an AArch64 `blr x1` instruction to invoke a callback supplied by the
native runner. The ELF loader validates the **entire ten-instruction sequence**
and accepts only the fixed layout shown by `tools/make_abi_elf.py`. This is not
arbitrary executable loading or a general pointer-based FFI.

`native_runner` (PD) -> `console_service` (NEW, separately isolated PD) uses
Microkit channel (runner ID 1, console ID 0) and a single shared `console_mailbox`.
The request is one-way; the callback does not block for a reply. A higher-
priority console service prints the requested integer using Microkit's
**debug output**, not the PL011 UART driver. The native runner also reports its
result via its existing worker mailbox to the application manager and shell.

On ARM64 the C-generated function uses a 32-byte stack frame, a single bounded
indirect callback call, register arithmetic and a return; no arbitrary IO,
MMIO, memory allocation, process creation, relocations, shared libraries,
privileged instructions, or `svc` system calls are permitted.

This is a *small application ABI prototype*, not a production security sandbox.
Code validation is only suitable for this exact allowlisted instruction sequence.
Moving to general ELF requires ELF load segments, private stacks, ABI, fault
handling, demand paging and a trustworthy policy for executable memory.
