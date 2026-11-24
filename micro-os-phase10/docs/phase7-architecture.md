# Micro-OS v0.7 native ELF execution prototype

```text
shell: elfrun /apps/return42.elf
   |
   | FS load request (shared mailbox + notification)
   v
MicroFS PD --(block IPC)--> VirtIO block-driver PD --> QEMU disk.img
   |
   | ASCII base64 (172 bytes), machine-readable FS reply
   v
terminal PD -- validate ELF64+opcodes --> native_code page (RW in terminal)
   |                                         native_code page (RX in runner)
   | app request IPC                         ^
   v                                         |
app_manager PD -- native IPC --> native_runner PD -- call entry offset 120
   |                                          |
   <------------- return 42 -----------------+
   |
   v
terminal prints native ELF returned 42
```

* Microkit 2.4.0 protection domains are **preallocated**; `elfrun` is NOT `fork` or `execve`.
* The loader currently accepts only exactly 128-byte single-segment ET_DYN with a two-instruction return function. `native_runner` has no hardware MMIO mappings.
* Both protection domains have access to the **same physical code frame** but different virtual mapping permissions. Terminal cannot execute the page; runner cannot write the page. The manager only receives a status notification and cannot map the code.
* Instruction cache maintenance matters on AArch64. This prototype targets Cortex-A53 QEMU and uses 64-byte cache-line assumptions.
* A large ELF or file with sections/symbols, external functions, relocations, or BSS will be rejected.
