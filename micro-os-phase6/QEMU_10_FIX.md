# QEMU 10.2 compatibility patch for Micro-OS v0.4

## Root cause

On QEMU 10.2 the ARM `virt` machine does **not** accept the `virtio-mmio-transports` machine property. The original package therefore failed before the bootloader ran.

## Why removing the property alone is not sufficient

Without the option, the `virt` machine has 32 VirtIO-MMIO transports. The first automatically assigned `virtio-blk-device` uses slot 31, **0x0a003e00**, IRQ **79** (GIC INTID), not slot 0 (`0x0a000000`, IRQ 48). Microkit maps pages, so `system.xml` maps `0x0a003000` and `src/virtio_blk.c` adds register offset **0xe00**. The test fixture now models that offset too.

## Files changed

- `Makefile`: removes unsupported machine property.
- `system.xml`: maps the correct page and IRQ 79.
- `src/virtio_blk.c`: adds transport offset 0xe00 to register accesses.
- `tests/test_virtio.c`: adjusts simulated device registers accordingly.
- `README.md`, `docs/architecture.md`: update documentation.

## Test

```sh
make check
make test
make clean && make
make run
```

An existing `disk.img` in your **old** v0.4 folder is preserved. If you copy it to this new folder, it will be reused by `make run`. Full seL4/QEMU boot still needs testing on your Ubuntu machine.

Sources: QEMU ARM `virt` machine documentation and `hw/arm/virt.c` (VirtIO MMIO transport ordering).
