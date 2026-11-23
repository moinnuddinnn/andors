# Micro-OS v0.4 Architecture

```text
 USER                              Microkit protection domains
 Keyboard                           (independent address spaces)
   |            [ terminal ]
   +--> UART --> shell, commands
          ^           |  FS request/reply + notification
          |           v
   [PL011 driver]   [ filesystem service ]
  MMIO 0x09000000      | private RAM filesystem + two-slot snapshot logic
   IRQ 33              | block sector mailbox + notification
                       v
                  [ VirtIO block driver ]
                  MMIO 0x0a000000, IRQ 48
                  split virtqueue, DMA
                       |
                   QEMU disk.img (raw)
──────────────────────── seL4 microkernel ────────────────────────
              QEMU virt AArch64 / Cortex-A53
```

## Boundary design

The terminal cannot access the raw filesystem data or the disk device. It only sees the `fs_mailbox`. The filesystem cannot access VirtIO MMIO registers, DMA buffers, or UART MMIO; it can issue 512-byte read/write requests over the `block_mailbox`. The VirtIO block driver is the sole holder of the VirtIO device and DMA memory mapping, while the UART driver has only PL011 MMIO.

`system.xml` uses an auto-assigned, physically contiguous `blk_dma` region, with its chosen **physical address** patched into the block driver by `<setvar symbol="blk_dma_paddr" region_paddr="blk_dma" />` and a **virtual address** patched separately by `setvar_vaddr="blk_dma_vaddr"`. Passing a user-space *virtual* pointer to the VirtIO device would be wrong on seL4.

## VirtIO-MMIO setup

The QEMU command creates **exactly one** VirtIO-MMIO transport by using `virtio-mmio-transports=1`, placing the first device at 0x0a000000 with GIC INTID 48. The modern transport identifies itself by magic `0x74726976`, version 2 and block device ID 2. The driver negotiates mandatory `VIRTIO_F_VERSION_1`, allocates an 8-descriptor split virtqueue, and uses a 3-element descriptor chain per request:

```text
  desc[0]  request header (16 bytes, readable by device)
     |
  desc[1]  512-byte sector (writable by device on READ, readable on WRITE)
     |
  desc[2]  one-byte status (writable by device)
```

All device DMA addresses are physical, from the `blk_dma` region. The guest waits asynchronously for IRQ completion, reads the used ring and status byte, acknowledges the device interrupt and its seL4 IRQ capability, then notifies the filesystem. Reads and writes are sector-aligned and single-request-at-a-time. Test code validates the layout but cannot prove real QEMU behavior.

## Persistence layout

```text
Disk sector   Content
────────────  ─────────────────────────────────────────────────────
0             Snapshot slot A header: magic, version, generation,
              payload byte length, CRC32 of payload
1..32         Snapshot slot A payload (state of all 48 nodes + cwd)
33            Snapshot slot B header
34..65        Snapshot slot B payload
66..          Unused/reserved
```

Snapshot size: 32 payload sectors × 512 bytes = 16 KiB, enough for 48 nodes × 292 bytes + cwd (the compiler must enforce this via `_Static_assert`). Writes proceed to the inactive payload *first*, then commit that slot's header as the final write. A later boot reads both headers, selects the newest candidate, validates its CRC and filesystem structure, and falls back to the other slot if necessary. This retains the last successful checkpoint in the common case of an interrupted new checkpoint, but is **not** a guarantee of crash-atomic persistence under all storage reorderings or host failures.

A blank disk loads seeded `/home` and `/system` directories; `sync` is required to create the first checkpoint. Subsequent changes remain volatile until the next `sync`.

## Testing vs. validation

Host tests (`make test`):
- Compile the real shell and filesystem C source against mocked Microkit notifications.
- Simulate 512-byte read/write requests to disk sectors and multiple boots.
- Confirm checkpoint generation, restoration, and CRC failure fallback.
- Compile and exercise the real VirtIO driver C source using a fake MMIO/virtqueue memory map.

Unverified before a successful Ubuntu integration run:
- Complete Microkit tool XML validation and ELF linking against SDK 2.4.0.
- QEMU VirtIO MMIO register behavior, IRQ delivery, DMA coherency in this precise runtime.
- Persistent writes surviving genuine QEMU termination and relaunch.
