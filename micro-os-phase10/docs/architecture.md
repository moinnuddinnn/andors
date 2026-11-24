# Micro-OS v0.6 architecture

Eight statically provisioned protection domains on seL4 Microkit 2.4.0:

| PD | Role | Externally accessible memory |
| --- | --- | --- |
| uart_driver | QEMU PL011 driver | UART MMIO 0x09000000, RX/TX rings |
| block_driver | VirtIO modern MMIO block | device page 0x0a003000 + 0xe00, DMA, block mailbox |
| filesystem | Persistent RAM snapshot service | FS & block mailboxes |
| terminal | Interactive shell and program-file loading | RX/TX rings, FS & application mailboxes |
| app_manager | Requests routing | App manager + calc + counter + script VM mailboxes |
| calculator | Isolated C worker | Calculator worker mailbox |
| counter | Isolated C worker | Counter worker mailbox |
| script_vm | Bounded text-program interpreter | Script worker mailbox only |

FS operation `load` is a read-only, status-prefixed response to terminal; the terminal itself owns the only FS mailbox. `exec` is a two-stage asynchronous operation: when the load response arrives, the terminal sends an application request to the manager; when the worker completes the terminal prints its response and prompts. This serializes the access to both mailboxes, which are one-outstanding-request prototypes.

Each worker is a preconfigured PD, running a fixed C image from Microkit's boot image; there is no dynamic thread/address-space creation. In particular, the script file does not become executable ARM code. The script VM reads its source as data, runs a bounded interpreter, and returns a bounded text reply.

### Limitations

- Not a security-hardened interpreter, general-purpose OS process loader or production filesystem.
- Scripts cannot write to files directly, nor execute arbitrary C/ELF code.
- Shell ignores typeahead while waiting for an asynchronous response.
- No concurrent application jobs: one outstanding app request at a time.
- File contents can be up to 255 bytes; shell command limit 255 characters.
- Persistent state still requires explicit `sync`.
- Native ELF support is a distinct future subsystem requiring direct seL4 allocation/mapping APIs or an evolved Microkit approach.
