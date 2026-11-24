/* Micro-OS v1.1: a bounded, asynchronous console-service request queue.
 * Microkit notifications wake the reader; data is in the shared SPSC ring.
 * The service uses Microkit's debug output, NOT direct UART hardware access.
 */
#include <stdint.h>
#include <microkit.h>
#include "console_protocol.h"
uintptr_t console_mailbox_vaddr = 0;

static void emit_decimal(uint32_t number)
{
    char digits[11]; unsigned n=0;
    do { digits[n++]=(char)('0'+number%10u); number/=10u; }
    while(number && n<sizeof(digits));
    char output[12];
    for(unsigned i=0;i<n;++i) output[i]=digits[n-i-1u];
    output[n]='\0';
    microkit_dbg_puts(output);
}
static void emit_hex(uint32_t value)
{
    char output[11]="0x00000000";
    static const char table[]="0123456789ABCDEF";
    for(unsigned i=0;i<8u;++i)
        output[2+i]=table[(value >> (28u-4u*i)) & 0xfu];
    microkit_dbg_puts(output);
}
void init(void) {
    microkit_dbg_puts("[CONSOLE] queued DEC/HEX ABI service ready\n");
}
void notified(microkit_channel channel)
{
    if(channel!=0) return;
    console_mailbox_t *q=(console_mailbox_t*)console_mailbox_vaddr;
    uint32_t read=__atomic_load_n(&q->read_seq,__ATOMIC_RELAXED);
    for (;;) {
        uint32_t write=__atomic_load_n(&q->write_seq,__ATOMIC_ACQUIRE);
        if(read==write) break;
        console_request_t r=q->requests[read % CONSOLE_QUEUE_CAP];
        if(r.op==CONSOLE_OP_PRINT_DEC) {
            microkit_dbg_puts("[CONSOLE] decimal: ");
            emit_decimal(r.value);
            microkit_dbg_puts("\n");
        } else if(r.op==CONSOLE_OP_PRINT_HEX) {
            microkit_dbg_puts("[CONSOLE] hexadecimal: ");
            emit_hex(r.value);
            microkit_dbg_puts("\n");
        } else {
            microkit_dbg_puts("[CONSOLE] invalid operation discarded\n");
        }
        ++read;
        __atomic_store_n(&q->read_seq,read,__ATOMIC_RELEASE);
    }
}
