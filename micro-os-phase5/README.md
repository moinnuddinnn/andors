# MICRO-OS v0.5 — Isolated C applications on seL4 Microkit

This builds directly on the working v0.4 persistent-storage project.
The **VirtIO-QEMU modern-mode fix** remains enabled:
`-global virtio-mmio.force-legacy=false`, at MMIO `0x0a003e00` (IRQ 79).

## What's new

v0.5 has **seven boot-configured seL4 Microkit protection domains**:

1. `uart_driver` — PL011 UART MMIO / IRQ 33
2. `block_driver` — VirtIO-MMIO disk / IRQ 79
3. `filesystem` — RAM tree with durable checkpoint snapshots
4. `terminal` — command line
5. `app_manager` — dispatches application requests and lists PDs
6. `calculator` — independent compiled C application domain
7. `counter` — independent compiled C application domain

Applications have no UART, block device, or filesystem address mappings.
They each share a separate 4KiB request/response region only with the
manager. `run` sends a command to an *already present* protection domain.
`ps` shows a statically configured registry with display-only IDs and job
counts, **not** a real dynamically created process table.

**This is not yet an ELF executable loader**, nor `fork`, `exec`, or a real
process-creation API. Microkit v2.4.0 is designed for statically structured
systems. See: https://docs.sel4.systems/projects/microkit/manual/2.4.0/

## Prerequisites

Ubuntu Linux + `make`, `gcc`, `aarch64-linux-gnu-gcc`,
`aarch64-linux-gnu-ld`, `qemu-system-aarch64` and a sibling directory
`../microkit-sdk-2.4.0` matching the tutorial's QEMU AArch64 debug build.

## Install alongside the working v0.4

```sh
cd ~/andros
unzip ~/Downloads/micro-os-phase5.zip
cd micro-os-phase5
```

If your existing v0.4 virtual disk contains valuable files, copy it rather
than starting with an empty disk:

```sh
# Only do this once, before your first v0.5 'make run'.
cp ../micro-os-phase4/disk.img ../micro-os-phase4/disk.img.backup
cp ../micro-os-phase4/disk.img ./disk.img
```

We have **not changed** the filesystem snapshot layout, so existing v0.4
files should still load. This makes a *copy*; updates in v0.5 won't appear in
v0.4 unless you later intentionally copy the disk back.

Do not run `make reset-disk` unless you deliberately want to erase saved files.
`make clean` deletes only compiled outputs; it doesn't erase `disk.img`.

## Build and run

```sh
make check
make test
make clean
make
make run
```

The Makefile preserves the QEMU modern VirtIO mode setting. If the SDK isn't
at its normal sibling path, pass `MICROKIT_SDK=/absolute/path/to/sdk` to make.
Exit QEMU with **Ctrl+A**, then **X**.

## Commands to test

```text
micro-os> apps
micro-os> run calc 7 + 5
calculator: 7 + 5 = 12
micro-os> run calc -12 * 4
calculator: -12 * 4 = -48
micro-os> run calc 8 / 0
Calculator: division by zero is undefined.
micro-os> run counter 7
counter: 1 2 3 4 5 6 7
micro-os> ps
...
micro-os> ls
micro-os> cat /projects/hello.txt
micro-os> sync
```

The calculator does **integer** arithmetic with operands in the range
`-1000000..1000000`. The counter accepts `1..30`. Requests are processed
one at a time through shared mailboxes and notifications.

`apps` discovers the two compiled-in apps, `run` dispatches a bounded job,
`ps` reports static protection-domain information and the number of completed
jobs. It doesn't start new address spaces or track scheduler thread state.

## Source code

- `src/terminal.c` — original shell + `apps`, `run`, `ps` integration
- `src/app_manager.c` — sequence-aware IPC dispatcher
- `src/app_calc.c` — isolated calculator application
- `src/app_counter.c` — isolated counter application
- `src/app_protocol.h` — single-request shared mailbox layouts
- `src/app_utils.h` — minimal freestanding string and integer utilities
- Existing `src/uart_driver.c`, `src/filesystem.c`, `src/virtio_blk.c`
- `system.xml` — 7 distinct PDs; each application's mailbox is separate
- `tests/test_apps.c` — host-side actual-source application IPC simulation
- `tests/test_persistence.c` — old disk snapshot regression test
- `tests/test_virtio.c` — fake-MMIO VirtIO regression test

## What is and is not tested

**Tested:** `make test` passes 3 host-side suites, including command routing,
calculator arithmetic, invalid inputs, counter output, filesystem snapshot
restore, and fake-MMIO block operations. All seven production C source files
were successfully compiled into AArch64 objects with Clang and a stub
Microkit header. XML was parsed successfully.

**Not tested here:** final SDK `microkit` image generation, seL4/QEMU boot,
real hardware isolation enforcement, and interactive command execution in
QEMU. Perform `make run` on your machine and report any output errors.

## Troubleshooting

- Compilation fails at `asm`: ensure `-std=gnu11` (not strict `-std=c11`).
- VirtIO missing: ensure the QEMU command contains
  `-global virtio-mmio.force-legacy=false`. The v0.5 Makefile has it.
- Existing files missing: make sure you copied the correct `disk.img` before
  starting and hadn't run `make reset-disk`.
- Shell stops responding after `run`: check channel IDs 2 (terminal to
  manager), 1/2 (manager to apps), and shared mailbox mappings in `system.xml`.
- No interactive key input: confirm `-serial mon:stdio` and `-nographic`.

## Next phase

To load new programs from storage at runtime (rather than compile each app
into a boot image), we need a proper seL4 resource manager that can allocate
untyped memory, create/retype kernel objects, manage address spaces and
capabilities, parse ELF files, and create/schedule threads. We will plan
that separately from Microkit's static PD mechanism.
