# Phase 4 host tests

`make test` runs:

1. `test_persistence.c`: actual terminal and filesystem sources with a simulated Microkit notification scheduler and emulated in-memory sector disk. Tests initial blank disk, `mkdir`, `write`, `sync`, restart, another commit, restart, corruption of latest checkpoint, restart and fallback to previous checkpoint.
2. `test_virtio.c`: actual VirtIO block driver source with fake MMIO registers and DMA memory. Checks modern VirtIO negotiation, queue physical addresses, chained descriptors, byte copies, IRQ completion and notification. This is a **unit test** only, not a functional VirtIO/QEMU emulator.

Both tests intentionally use `tests/include/microkit.h` rather than the Microkit SDK. A full SDK integration build and QEMU boot are separate, mandatory milestones.
