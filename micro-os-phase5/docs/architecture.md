# Micro-OS v0.5 — static application domains + IPC

```text
 PL011 MMIO+IRQ                         VirtIO-MMIO+IRQ -> disk.img
      |                                          |
 UART driver PD                              Block driver PD
      |    rx/tx rings                         block mailbox
      v                                          |
 terminal PD <---- fs mailbox ----> filesystem PD
      |
      |  app mailbox + notifications (terminal ch2 <-> manager ch0)
      v
 app_manager PD
      |                        |
      | calc mailbox          | counter mailbox
      | manager ch1            | manager ch2
      v                        v
 calculator PD             counter PD
```

All C components use Microkit 2.4.0 entry points `init()` and `notified()`.
A channel carries *notifications*, not arguments. The shared 4KiB mailbox
holds request text and a sequence number; one client request is outstanding
at a time. Writers publish with release atomics and readers use acquire
loads. The app manager dispatches to one worker and returns that worker's
reply to the shell through the same request-response pattern.

The application manager is not a dynamic kernel thread or process manager.
It mediates *already-created*, statically mapped independent protection
domains. Neither application maps the UART MMIO, VirtIO MMIO, the other
application's mailbox, or filesystem memory. At boot, each app starts its
Microkit event loop and subsequently executes a bounded task upon a notify.

## IPC channel identifiers

| Link | Side A | Side B |
| --- | --- | --- |
| UART <-> terminal | UART `0` | terminal `0` |
| FS <-> terminal | FS `0` | terminal `1` |
| FS <-> block | FS `1` | block `0` |
| App manager <-> terminal | app manager `0` | terminal `2` |
| App manager <-> calculator | app manager `1` | calculator `0` |
| App manager <-> counter | app manager `2` | counter `0` |

## Important limitations

- No dynamic `fork`/`exec`/ELF loading.
- `ps` IDs are display-only registry IDs, not kernel process IDs.
- Apps execute bounded one-shot tasks; they are not persistent interactive
  programs, and there is no `kill` command.
- One application or FS request is outstanding at a time; shell typeahead
  is lost while a request is processing.
- Shared-memory ring output can drop characters when full.
- Snapshot persistence uses v0.4's experimental disk format and requires
  explicit `sync`.
