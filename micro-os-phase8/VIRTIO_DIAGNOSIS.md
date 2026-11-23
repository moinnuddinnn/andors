# Micro-OS v0.4 — VirtIO-MMIO diagnosis

## Why the previous failure happened

`[BLK] VirtIO 1.0+ MMIO block device missing` was emitted when *any* one of three comparisons failed (magic, version, ID). This included a perfectly present but **legacy MMIO version 1** transport. QEMU's `virtio-mmio` device has a `force-legacy` property whose default can produce version 1. This edition adds `-global virtio-mmio.force-legacy=false` to `make run` and prints all three register values before device initialization.

## What to do

1. Back up your existing project. To retain its existing `disk.img`, copy only **Makefile**, **src/virtio_blk.c**, **tests/test_virtio.c** from this package to your `~/andros/micro-os-phase4/` directory, or run this package as its own new directory with a fresh disk.
2. From the directory where you applied the changes run `make test && make clean && make && make run`.
3. Watch the first `[BLK] Probe` line. It identifies the actual issue rather than guessing.

## Interpret the register values

| MMIO results | Meaning | Next action |
| --- | --- | --- |
| `MAGIC=0x74726976 VERSION=0x00000002 DEVICE_ID=0x00000002` | Modern VirtIO block device found, proceed to queue initialization. | Look for `[BLK] ...online`. If not online, check the *next* diagnostic message. |
| `MAGIC=0x74726976 VERSION=0x00000001 DEVICE_ID=0x00000002` | QEMU exposed **legacy** VirtIO-MMIO to our modern-only driver. | Confirm the `-global virtio-mmio.force-legacy=false` Makefile option is actually on the QEMU command line. |
| `MAGIC=0x74726976` with `DEVICE_ID=0x00000000` | VirtIO-MMIO transport exists here but **no block backend is attached**. | Find the actual assigned slot using QEMU monitor (`Ctrl+A`, then `C` to toggle; `info qtree`). Confirm `-device virtio-blk-device,drive=disk0`. |
| `MAGIC` not `0x74726976` (often `0x00000000` or `0xffffffff`) | Likely incorrect physical MMIO mapping, wrong offset, or an inaccessible region. | Verify `<memory_region name="virtio_mmio" phys_addr="0x0a003000" size="0x1000"/>` and `MMIO_TRANSPORT_OFFSET = 0xe00`. Check QEMU's memory map or device tree. |

The QEMU ARM `virt` board generally creates 32 VirtIO-MMIO transports at bases `0x0a000000 + i*0x200`; assignment to a particular slot depends on QEMU configuration/order. With only one automatically attached `virtio-blk-device`, QEMU's conventional ordering selects slot 31 (`0x0a003e00`), corresponding to GIC INTID 79.

## Confirm QEMU itself sees the block device

During `make run`, press **Ctrl+A**, then **C** (if the combined monitor/stdio supports this sequence). In the QEMU monitor run:

```text
info qtree
info mtree
```

Find a `virtio-mmio` transport containing a `virtio-blk-device`. The memory tree can help identify the corresponding physical address, and the command-line `-drive ... id=disk0` must be attached to `-device virtio-blk-device,drive=disk0`.

**Do not erase `disk.img`.** `make clean` only removes `build/`, not your existing disk.

References: https://gitlab.com/qemu-project/qemu/-/work_items/1342 ; https://www.qemu.org/docs/master/system/arm/virt.html ; https://github.com/qemu/qemu/blob/master/hw/virtio/virtio-mmio.c .
