# Micro-OS v0.3 — service architecture

```text
                keyboard / terminal window
                           │
                 QEMU ARM PL011 UART
              MMIO 0x09000000 · IRQ 33
                           │
                     seL4 / Microkit
                           │
               MMIO map + IRQ capability
                           │
             ┌─────────────▼──────────────┐
             │ UART driver protection     │
             │ domain (priority 200)      │
             └─────────────┬──────────────┘
                           │
               two 4 KiB shared pages:
               RX/TX ring + notifications
                           │
             ┌─────────────▼──────────────┐
             │ Terminal protection domain │
             │ shell, prompt, parser      │
             │ (priority 100)             │
             └─────────────┬──────────────┘
                           │
                  4 KiB shared mailbox:
                  request_seq, operation,
                  path, text, reply,
                  response_seq
                  + notifications
                           │
             ┌─────────────▼──────────────┐
             │ RAMFS protection domain    │
             │ hierarchy + file bytes    │
             │ (priority 150)             │
             └────────────────────────────┘
```

## Why the filesystem is a real server

Unlike placing file-handling functions in `terminal.c`, this design gives the
filesystem its own ELF, protected address space, scheduling context and
private static file data. The shell does not map that private file data.
The shell and server communicate by sending a notification after publishing
a request in a shared mailbox; the server processes the operation and sends
another notification after publishing the reply. Notification channels have
no inherent message payload and can coalesce, so sequence fields allow the
receiver to ignore duplicate/spurious notifications.

## Mailbox protocol

One request at a time:

1. Terminal copies `operation`, `path`, `data` into shared memory.
2. Terminal stores `request_seq` with release ordering, then notifies the FS.
3. FS observes the changed sequence with acquire ordering and handles it.
4. FS writes `reply` then publishes `response_seq` with release ordering.
5. FS notifies the terminal. Terminal reads response_seq with acquire ordering
   and prints the reply to the UART shared TX ring.

The protocol is designed for one trusted client; shared writable memory is not
an adversarial security boundary. Further work could use separate request /
response pages and stricter mappings, error paths, flow control and client IDs.

## Allocation and capacity

- `node_t` entries live in a fixed array in the filesystem process.
- Entries contain `name`, `parent` index, directory/file flag and in-memory
  file bytes.
- 48 entries maximum including the root. Files store up to 255 bytes each.
- Root is `/`. Initial directories `/home` and `/system` include two files.
- Paths may contain `.` and `..` components. There is no on-disk persistence.

## Next steps

Add flow control and mailbox timeouts, create tests with the actual SDK, add
block-backed storage, then decide whether Microkit's static PD model remains
suitable for dynamic user processes.
