#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console_protocol.h"
extern uintptr_t console_mailbox_vaddr;
void init(void);
void notified(int);
static char output[2048];
void microkit_dbg_puts(const char *s) {
    strncat(output,s,sizeof(output)-strlen(output)-1);
}
void microkit_notify(int channel) { (void)channel; }
void microkit_irq_ack(int channel) { (void)channel; }
static void must(int ok, const char *why) {
    if (!ok) { fprintf(stderr,"FAIL: %s (%s)\n",why,output); exit(1); }
}
int main(void) {
    console_mailbox_t queue={0};
    console_mailbox_vaddr=(uintptr_t)&queue;
    init();
    /* Two requests are produced BEFORE a single notification is delivered. */
    must(console_enqueue(&queue,CONSOLE_OP_PRINT_DEC,42),"enqueue dec");
    must(console_enqueue(&queue,CONSOLE_OP_PRINT_HEX,42),"enqueue hex");
    must(queue.write_seq==2u && queue.read_seq==0u,"both queued");
    notified(0);
    const char *dec=strstr(output,"[CONSOLE] decimal: 42\n");
    const char *hex=strstr(output,"[CONSOLE] hexadecimal: 0x0000002A\n");
    must(dec && hex && dec<hex,"both serviced in order despite notification coalescing");
    must(queue.read_seq==2u,"reader acknowledged two entries");
    size_t length=strlen(output);
    notified(0);
    must(strlen(output)==length,"duplicate notification does not replay");
    must(!console_enqueue(&queue,99u,0),"invalid operation rejected");
    /* Fill ring; consumer stays idle. Full means no overwrite of old values. */
    for(unsigned i=0;i<CONSOLE_QUEUE_CAP;++i)
        must(console_enqueue(&queue,CONSOLE_OP_PRINT_DEC,100u+i),"ring capacity");
    must(!console_enqueue(&queue,CONSOLE_OP_PRINT_HEX,0),"full ring rejects enqueue");
    notified(0);
    must(queue.read_seq==queue.write_seq,"consumer drains ring after wrap");
    must(strstr(output,"[CONSOLE] decimal: 107\n")!=NULL,"last queued value preserved");
    must(console_enqueue(&queue,CONSOLE_OP_PRINT_HEX,0xABCDEF10u),"enqueue after drain");
    notified(0);
    must(strstr(output,"[CONSOLE] hexadecimal: 0xABCDEF10\n")!=NULL,"hex formatter");
    puts("PASS: queued DEC/HEX ABI survives coalesced notifications, wraps, rejects overflow");
    return 0;
}
