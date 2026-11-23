/* Micro-OS: interactive shell isolated from serial hardware.
 * Uses two shared-memory SPSC rings and a Microkit notification channel.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "ring.h"

#define CH_UART_DRIVER 0
#define MAX_LINE 128u

/* Populated by the Microkit image builder via setvar_vaddr. */
uintptr_t rx_ring_vaddr = 0;
uintptr_t tx_ring_vaddr = 0;

static char line[MAX_LINE];
static unsigned line_length = 0;
static bool last_was_cr = false;

static void emit_char(char c)
{
    /* Lines printed by this prototype are comfortably under 2 KiB.
     * A full ring drops output; future versions will add flow control. */
    (void)ring_push((ring_t *)tx_ring_vaddr, (uint8_t)c);
}

static void emit(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            emit_char('\r');
        }
        emit_char(*s++);
    }
}

static bool str_equal(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_with(const char *a, const char *prefix)
{
    while (*prefix) {
        if (*a++ != *prefix++) {
            return false;
        }
    }
    return true;
}

static void prompt(void)
{
    emit("micro-os> ");
}

static void run_command(const char *cmd)
{
    if (str_equal(cmd, "")) {
        return;
    } else if (str_equal(cmd, "help")) {
        emit("Commands:\n");
        emit("  help         Show commands\n");
        emit("  about        About Micro-OS\n");
        emit("  version      System version\n");
        emit("  echo <text>  Print text\n");
        emit("  clear        Clear the terminal\n");
        emit("  devices      List available device drivers\n");
        emit("  ipc          Explain service communication\n");
    } else if (str_equal(cmd, "about")) {
        emit("Micro-OS: a C operating-system experiment using seL4 Microkit.\n");
        emit("The shell and UART driver run in separate protection domains.\n");
    } else if (str_equal(cmd, "version")) {
        emit("Micro-OS v0.2 / seL4 + Microkit 2.4.0 / AArch64 QEMU\n");
    } else if (str_equal(cmd, "echo")) {
        emit("\n");
    } else if (starts_with(cmd, "echo ")) {
        emit(cmd + 5);
        emit("\n");
    } else if (str_equal(cmd, "clear")) {
        emit("\x1b[2J\x1b[H");
    } else if (str_equal(cmd, "devices")) {
        emit("uart0: ARM PL011 serial device (IRQ 33, driver PD)\n");
    } else if (str_equal(cmd, "ipc")) {
        emit("Terminal <-> UART: Microkit notifications + shared-memory rings.\n");
        emit("Only the UART driver maps the device's MMIO registers.\n");
    } else {
        emit("Unknown command: ");
        emit(cmd);
        emit("\nType 'help' for available commands.\n");
    }
}

static void handle_input(uint8_t ch)
{
    if (ch == '\n' && last_was_cr) {
        last_was_cr = false; /* ignore LF after CR */
        return;
    }
    last_was_cr = (ch == '\r');

    if (ch == '\r' || ch == '\n') {
        emit("\n");
        line[line_length] = '\0';
        run_command(line);
        line_length = 0;
        prompt();
    } else if (ch == 8u || ch == 127u) {
        if (line_length > 0u) {
            --line_length;
            emit("\b \b");
        }
    } else if (ch >= 32u && ch <= 126u) {
        if (line_length < MAX_LINE - 1u) {
            line[line_length++] = (char)ch;
            emit_char((char)ch);  /* echo input */
        }
    }
}

void init(void)
{
    emit("\n");
    emit("=============================================\n");
    emit("  MICRO-OS v0.2  |  seL4 + Microkit 2.4.0\n");
    emit("  Two protection domains, one real UART\n");
    emit("=============================================\n");
    emit("Welcome! Type 'help' and press Enter.\n\n");
    prompt();
    microkit_notify(CH_UART_DRIVER);
}

void notified(microkit_channel channel)
{
    if (channel != CH_UART_DRIVER) {
        return;
    }
    ring_t *rx = (ring_t *)rx_ring_vaddr;
    uint8_t byte;
    while (ring_pop(rx, &byte)) {
        handle_input(byte);
    }
    microkit_notify(CH_UART_DRIVER); /* flush accumulated echo and output */
}
