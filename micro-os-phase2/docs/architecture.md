# Phase 2 architecture

```
              terminal.elf  (PD, priority 100)
             shell, line editing, built-in commands
                       |            ^
                  tx_ring         rx_ring
                       v            |
               UART driver.elf  (PD, priority 200)
               PL011 TX (poll) / RX IRQ (33)
                             |
                      MMIO 0x09000000
                             |
                       QEMU virt UART
```

Both PDs are user-space protection domains provided by seL4 Microkit. They share
no code/data except pages explicitly mapped in system.xml. Both use a Microkit
notification channel at local ID 0. The UART driver receives UART IRQ 33 as
local channel 1 and calls microkit_irq_ack(1) after clearing the device IRQ.

The driver has the only UART MMIO mapping; the terminal cannot touch it.
The RX ring has one producer (UART) and one consumer (terminal); the TX ring
has one producer (terminal) and one consumer (UART). Both use release/acquire
atomic operations for head and tail indices. Coalesced notifications are safe
because each consumer drains all available data.

Next: add flow control, a service registry, multiple shell/client domains, and
an in-memory file system. Conventional dynamic processes require more
infrastructure than Microkit's static PD configuration alone provides.
