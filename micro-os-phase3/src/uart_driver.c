/* Micro-OS: isolated PL011 UART driver for QEMU virt / AArch64.
 * This is the ONLY protection domain mapped to the UART registers.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "ring.h"

#define CH_TERMINAL 0
#define CH_UART_IRQ 1

/* Rewritten by Microkit from system.xml. Must have external linkage. */
uintptr_t uart_base_vaddr = 0;
uintptr_t rx_ring_vaddr = 0;   /* UART -> terminal */
uintptr_t tx_ring_vaddr = 0;   /* terminal -> UART */

#define UART_DR     0x000u
#define UART_FR     0x018u
#define UART_LCRH   0x02cu
#define UART_CR     0x030u
#define UART_IMSC   0x038u
#define UART_ICR    0x044u

#define UART_FR_RXFE (1u << 4)
#define UART_FR_TXFF (1u << 5)
#define UART_IM_RX   (1u << 4)
#define UART_IM_RT   (1u << 6)

static inline uint32_t uart_read(uint32_t off)
{
    return *(volatile uint32_t *)(uart_base_vaddr + off);
}

static inline void uart_write(uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(uart_base_vaddr + off) = value;
}

static void uart_putc(uint8_t byte)
{
    while ((uart_read(UART_FR) & UART_FR_TXFF) != 0u) {
        /* Short bounded-by-hardware busy wait for UART FIFO space. */
    }
    uart_write(UART_DR, byte);
}

static void uart_init(void)
{
    uart_write(UART_IMSC, 0u);       /* mask interrupts while configuring */
    uart_write(UART_CR, 0u);         /* disable UART */
    uart_write(UART_ICR, 0x7ffu);    /* clear pending IRQ sources */
    uart_write(UART_LCRH, 3u << 5);  /* 8-bit words, no FIFO (1-byte RX IRQ) */
    uart_write(UART_CR, (1u << 9) | (1u << 8) | 1u); /* RX, TX, UART enable */
    uart_write(UART_IMSC, UART_IM_RX | UART_IM_RT);
}

static void flush_terminal_output(void)
{
    ring_t *tx = (ring_t *)tx_ring_vaddr;
    uint8_t byte;
    while (ring_pop(tx, &byte)) {
        uart_putc(byte);
    }
}

static void accept_uart_input(void)
{
    ring_t *rx = (ring_t *)rx_ring_vaddr;
    uint8_t byte;
    bool any = false;

    /* Read every available character: notifications may be coalesced. */
    while ((uart_read(UART_FR) & UART_FR_RXFE) == 0u) {
        byte = (uint8_t)(uart_read(UART_DR) & 0xffu);
        if (ring_push(rx, byte)) {
            any = true;
        }
        /* If the ring fills, excess bytes are dropped in this prototype. */
    }

    /* Clear device IRQ *before* acknowledging the seL4 IRQ capability. */
    uart_write(UART_ICR, 0x7ffu);
    if (any) {
        microkit_notify(CH_TERMINAL);
    }
    microkit_irq_ack(CH_UART_IRQ);
}

void init(void)
{
    uart_init();
    microkit_dbg_puts("[UART] PL011 driver online (separate protection domain)\n");
}

void notified(microkit_channel channel)
{
    if (channel == CH_TERMINAL) {
        flush_terminal_output();
    } else if (channel == CH_UART_IRQ) {
        accept_uart_input();
    }
}
