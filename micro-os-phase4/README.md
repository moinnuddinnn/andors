# Micro-OS v0.4 — Persistent Storage (seL4 Microkit)

A small experimental operating-system prototype written in **GNU C11**, built on the **seL4 Microkit 2.4.0** SDK for **QEMU AArch64 `virt`**. This version extends the working v0.3 shell and RAM filesystem with a real, isolated **VirtIO 1.0+ MMIO block driver** and a **checkpoint-based persistent filesystem**.

> Important: The full Microkit image has **not** been built or booted in QEMU by the author of this package. Both host-side test suites pass, and all C components compile to AArch64 objects with Clang using the supplied test headers. Your Ubuntu/SDK setup is the integration test. Keep your working v0.3 directory as a backup.

## Prerequisites

On Ubuntu (the existing Microkit SDK and cross compiler from v0.3 are reused):

```bash
sudo apt update
sudo apt install -y make gcc gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-arm
```

Put this project alongside `microkit-sdk-2.4.0`:

```text
~/andros/
├── microkit-sdk-2.4.0/
├── micro-os-phase3/          # keep it — verified working
└── micro-os-phase4/
```

If the SDK is elsewhere, set `MICROKIT_SDK=/absolute/path/to/microkit-sdk-2.4.0` for each relevant `make` invocation.

## Build and boot

```bash
cd ~/andros/micro-os-phase4
make check
make test                   # host simulation of shell, FS, and VirtIO driver
make                         # compile AArch64 PDs and build loader.img
make run                     # creates disk.img ONCE and starts QEMU
```

`make disk` creates an **8 MiB raw disk image** named `disk.img` only if it doesn't already exist. `make run` reuses it, and `make clean` does **not** delete it.

**DO NOT use `make reset-disk` unless you want to permanently erase all saved files.**

The virtual board uses `-machine virt,virtualization=on,virtio-mmio-transports=1`, so a *single* VirtIO-MMIO transport is placed at physical `0x0a000000`, IRQ/GIC INTID 48. QEMU connects `virtio-blk-device` with `-device virtio-blk-device,drive=disk0`. Avoid adding additional VirtIO-MMIO devices unless the board configuration and driver discovery are revised.

## The reboot persistence experiment

After QEMU boots, type (without the `micro-os>` prefix):

```text
help
diskinfo
mkdir projects
cd projects
write hello.txt This file must survive a reboot!
cat hello.txt
fsinfo
sync
```

**Expected after `sync`:**

```text
Filesystem saved successfully (slot 0, generation 1).
```

The header is written **last**, after the payload sectors have completed.

Exit QEMU with **Ctrl+A, then X** (because the QEMU command uses `-serial mon:stdio`). Run again:

```bash
make run
```

Now type:

```text
cat /projects/hello.txt
```

Expected output:

```text
This file must survive a reboot!
```

To make another commit, modify files then run `sync` again. On each `sync`, the slots alternate. Try `diskinfo` to see the current active slot and generation. **Changes made since the last `sync` are not persistent.** `sync` saves the whole small filesystem snapshot, not individual files.

## What is new in v0.4?

* **Four isolated Microkit protection domains**: PL011 UART driver, interactive shell, filesystem service, VirtIO-MMIO block driver.
* **New communication channel**: filesystem ⇄ block driver through a shared mailbox and asynchronous Microkit notifications.
* **Real VirtIO-MMIO device access**: driver negotiates modern VirtIO features and programs a split virtqueue, including physically contiguous DMA memory. The filesystem cannot touch VirtIO registers.
* **New shell commands**: `diskinfo` and `sync`; `fsinfo` now reports unsaved changes.
* **Two alternating checkpoints**: 32 × 512-byte payload sectors plus a 512-byte header per slot (66 sectors total); magic, format version, generation, byte count, CRC-32.
* **Fallback on boot**: if the newer snapshot fails CRC validation, attempt the previous checkpoint. A completely blank disk creates a fresh filesystem.

Existing commands: `help`, `about`, `version`, `echo`, `clear`, `devices`, `ipc`, `pwd`, `ls`, `cd`, `mkdir`, `touch`, `write`, `append`, `cat`, `rm`, `rmdir`, `fsinfo`.

## Important limits

This is a **teaching prototype**, not a general-purpose or production-safe file system:

1. Files are limited to 255 bytes, there are 48 filesystem entries, and command paths have a 95-character limit. Filesystem state is serialized as a C struct; changing the struct requires a format migration/version bump.
2. `sync` must be run manually. QEMU uses `cache=writethrough` to request durable host-file writes, but the code **does not implement a VirtIO FLUSH request**, nor does it guarantee durability against sudden host power failure or arbitrary storage faults. Alternating slots and checksums aid recovery but are not full transactional storage.
3. There is one block request outstanding at a time; no concurrent callers, timeouts, retries, request cancellation, partial I/O, error recovery, or partition discovery. The OS deliberately stores snapshots in low-numbered raw disk sectors, **not FAT/ext4**.
4. UART RX can drop bytes when busy; the interactive terminal has a fixed ring and no typeahead. Avoid pasting very long commands, especially while `sync` is running.
5. This driver currently assumes QEMU `virt`'s coherent DMA semantics and a single VirtIO-MMIO transport. **Do not deploy this on real hardware without DMA/cache coherency, IOMMU/device isolation, and interrupt validation.**
6. The exposed block device is writable. Disk images from unknown sources should not be trusted.

## Troubleshooting

* **`make` complains about `asm`**: this project uses `-std=gnu11`, not strict `-std=c11`. Do not undo this setting.
* **`make check` passes but build fails**: check the *first* compiler error; `make check` does not compile source.
* **`virtio-blk: unavailable (RAM-only mode)`**: the block driver did not discover the expected device or a block I/O failed. Check the QEMU command in the Makefile and look for `[BLK]` messages above the shell. `sync` will refuse to report success.
* **New file is missing after reboot**: did `sync` print `Filesystem saved successfully`? Is the exact same `disk.img` being reused? Did `make reset-disk` run?
* **The QEMU build prints an exception/fault**: collect the **first fault line and register/PC printout**, plus any `[BLK]` messages, and the last 60 lines of the build log. This will identify whether the problem is a memory map, IRQ, or VirtIO transport issue.
* **OS hangs after `sync`**: could be a missing VirtIO completion IRQ. Check MMIO address `0x0a000000`, IRQ 48, and that the QEMU machine sets `virtio-mmio-transports=1`. This is the most important real-hardware integration check after successful unit tests.
* **Never manually `fsck`/`mount` the raw disk image as a host filesystem.** Its structure is custom and intentionally minimal.

## Source files

```text
src/uart_driver.c       PL011 UART-MMIO, IRQ 33
src/terminal.c          Shell and filesystem command parser
src/filesystem.c        Hierarchical RAMFS + disk checkpoint state machine
src/virtio_blk.c        VirtIO 1.0+ MMIO block driver, IRQ 48, split virtqueue
src/ring.h              UART RX/TX shared rings
src/fs_protocol.h       Terminal <-> filesystem mailbox
src/block_protocol.h    Filesystem <-> block driver sector mailbox
system.xml              All PDs, memory regions, IRQs, and channels
Makefile                AArch64 build, disk creation, QEMU run, host tests
tests/test_persistence.c   Simulated reboot/persistence/fallback integration
tests/test_virtio.c        Simulated MMIO/DMA descriptor/IRQ test
```

Read `docs/architecture.md` for details.

## Primary references

* [seL4 Microkit 2.4.0 User Manual](https://docs.sel4.systems/projects/microkit/manual/2.4.0/)
* [OASIS VirtIO 1.2 Specification](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)
* [QEMU ARM `virt` machine documentation](https://www.qemu.org/docs/master/system/arm/virt.html)
* [QEMU source: create_virtio_devices / HW IRQ map](https://github.com/qemu/qemu/blob/master/hw/arm/virt.c)
