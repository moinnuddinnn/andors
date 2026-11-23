# Micro-OS v0.2: UART driver + interactive shell

This is Phase 2 of our C + seL4 Microkit OS experiment. It uses **two independent
protection domains** running on `qemu_virt_aarch64` in the **Microkit 2.4.0 SDK**:

- `uart_driver`: maps the QEMU PL011 UART MMIO page and receives IRQ 33.
- `terminal`: parses shell commands, but cannot directly access UART registers.

They exchange bytes through **two independent shared-memory SPSC ring buffers**,
using a bidirectional Microkit **notification channel** to wake each other up.
Notifications carry no data themselves. They can coalesce, so each receiver
always drains the corresponding ring completely.

## Preserve your working Phase 1

Put `micro-os-phase2/` beside `micro-os-starter/` and `microkit-sdk-2.4.0/`.
For example (your folder may be `~/andros/`):

```
~/andros/
├── microkit-sdk-2.4.0/
├── micro-os-starter/    # Phase 1 — left unchanged
└── micro-os-phase2/     # This new project
```

On Ubuntu with the cross-compiler and QEMU already installed:

```bash
cd ~/andros/micro-os-phase2
make check
make
make run
```

The shell should appear after seL4 and Microkit's startup messages:

```
[UART] PL011 driver online (separate protection domain)
=============================================
  MICRO-OS v0.2  |  seL4 + Microkit 2.4.0
  Two protection domains, one real UART
=============================================
Welcome! Type 'help' and press Enter.

micro-os> help
...
```

Commands: `help`, `about`, `version`, `echo YOUR TEXT`, `clear`, `devices`, `ipc`.

Exit QEMU using **Ctrl+A**, then **X**. Do not run this image on real hardware;
the UART base address and IRQ are specific to the QEMU virt board.

## SDK stored in a different directory?

```bash
make check MICROKIT_SDK=/absolute/path/to/microkit-sdk-2.4.0
make run MICROKIT_SDK=/absolute/path/to/microkit-sdk-2.4.0
```

## What to study

- `system.xml`: memory regions, two PDs, UART IRQ, notification channel.
- `src/uart_driver.c`: memory-mapped device registers and IRQ acknowledgements.
- `src/terminal.c`: command interpreter and keyboard editing.
- `src/ring.h`: two-way data exchange using separate SPSC shared-memory rings.

## Prototype limitations

- No independent user processes, filesystem, or dynamic process creation yet.
- 2048-byte shared rings; overflow currently drops data, rather than
  implementing backpressure or a queueing protocol.
- UART TX is polling; RX is interrupt-driven.
- The driver uses hard-coded PL011 register definitions specific to QEMU virt.
- C11 atomic built-ins coordinate indices between the two PDs on the same CPU.
- The code is not production hardened; no flow control, raw terminal modes,
  process isolation tests, or recovery logic yet.

## Verified references

- Official Microkit v2.4.0 manual: https://docs.sel4.systems/projects/microkit/manual/2.4.0/
- UART tutorial (MMIO 0x09000000, IRQ 33): https://docs.sel4.systems/projects/microkit/tutorial/part1.html
- Shared memory and channels tutorial: https://docs.sel4.systems/projects/microkit/tutorial/part2.html

## Build verification caveat

The XML, Makefile command generation, and C syntax were checked in the packaging
environment, but we cannot run the full seL4 SDK/QEMU integration test here.
Please share your `make` output or any faults if this version does not boot.
