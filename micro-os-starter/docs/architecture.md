# Micro-OS architectural plan

## Boot path
QEMU -> Microkit loader -> seL4 kernel -> Microkit initialiser -> micro_os_init protection domain.

## Target microkernel architecture (planned)

- seL4 kernel: isolation, thread scheduling, address spaces, IPC, capabilities.
- Microkit: static declaration of protection domains, memory regions, and channels.
- UART driver PD: MMIO, interrupts, buffering, terminal I/O.
- Terminal/shell PD: command interpretation and application front-end.
- VFS PD: filesystem namespace and operations.
- RAM filesystem PD: initial storage backend.
- Service manager PD: discovery and lifecycle of predefined services.

## Security rule
Only the UART driver should receive the UART MMIO mapping and interrupt.
The terminal service uses IPC rather than accessing hardware directly.

## Limitations
Microkit statically describes resources and isolation relationships.
A conventional general-purpose OS with dynamic process loading requires
additional resource management, memory allocation, and executable loading;
it should not be presented as a built-in Microkit feature.
