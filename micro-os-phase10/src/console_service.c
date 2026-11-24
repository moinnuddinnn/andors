/* Micro-OS v1.0 demonstration: isolated one-way numerical console service.
 * The native runner requests the service over Microkit notifications + shared
 * memory. This prototype uses the Microkit debug output path, NOT direct UART
 * MMIO access and NOT a general-purpose print syscall/ABI.
 */
#include <stdint.h>
#include <microkit.h>
#include "console_protocol.h"
uintptr_t console_mailbox_vaddr = 0;
static uint32_t last_handled;

static void emit_decimal(uint32_t number) {
    char digits[11];
    unsigned n = 0;
    do {
        digits[n++] = (char)('0' + (number % 10u));
        number /= 10u;
    } while (number && n < sizeof(digits));
    char output[12];
    for (unsigned i = 0; i < n; ++i) output[i] = digits[n - i - 1u];
    output[n] = '\0';
    microkit_dbg_puts(output);
}
void init(void) {
    last_handled = 0;
    microkit_dbg_puts("[CONSOLE] independent native-ABI logging service ready\n");
}
void notified(microkit_channel ch) {
    if (ch != 0) return;
    console_mailbox_t *m = (console_mailbox_t*)console_mailbox_vaddr;
    uint32_t seq = __atomic_load_n(&m->request_seq, __ATOMIC_ACQUIRE);
    if (seq == last_handled) return;
    last_handled = seq;
    /* Exact decimal representation; no printf, libc or untrusted format string. */
    microkit_dbg_puts("[CONSOLE] native app printed: ");
    emit_decimal(m->number);
    microkit_dbg_puts("\n");
}
