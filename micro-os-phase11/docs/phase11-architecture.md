# Micro-OS v1.1: bounded native-to-console service queue

## Goal

Convert the v1.0 single-slot native console mailbox into a bounded FIFO and
exercise it twice from one native C invocation. Preserve existing MicroFS disk
format, VirtIO driver, and the ten preconfigured seL4 Microkit PDs.

## Message flow

```
MicroFS (persistent disk) --load--> terminal (ELF validator, RW code mapping)
                                           |
                                  app-manager request
                                           |
                                           v
                            native_runner (RX code, private RW data)
                                           |
       Clang native program(x=42)          | approved callback(op, value)
        service(1, 42)                     |   queue enqueue: DEC 42
        service(2, 42)                     |   queue enqueue: HEX 42
        return 42                          |   notifications are wakeups only
                                           v
                        shared console queue (8 x (op, value))
                                           |
                                           v
                                console_service PD
                             DEC 42     -> debug output
                             HEX 0x2A  -> debug output
                             acknowledge read_seq
```

`native_runner` is the sole producer and `console_service` the sole consumer.
Each publishes its own index using release semantics and loads the opposite
index with acquire semantics. If the ring is full, enqueue returns false rather
than discarding an earlier request. Native runner then reports a callback
count / queue-full error. The console PD needs RW mapping **only** for updating
`read_seq`; it has no PL011 or VirtIO hardware mapping.

## Native code restrictions

The supported ELF contains exactly one `PT_LOAD` executable segment, one 60-byte
Clang-generated function at offset 124, and no relocations or mutable segments.
The validator checks each AArch64 instruction word against an exact allowlist.
The function saves/restores a bounded stack frame and invokes the callback
pointer provided by the runner twice. This is intentionally more restricted
than general ELF execution and does not confer dynamic process creation.

## Known limitations

* One producer and one consumer; queue length 8, no blocking on full.
* Console service prints via Microkit debug I/O, not UART hardware.
* The native program may request only two predefined console operations.
* Untrusted arbitrary AArch64 code is not supported.
* Cross-domain completion and console print ordering are asynchronous.
* No native process lifecycle, per-program address spaces, or fault recovery.
