#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console_protocol.h"
extern uintptr_t console_mailbox_vaddr;
void init(void);
void notified(int);
static char output[300];
void microkit_dbg_puts(const char *s) { strncat(output,s,sizeof(output)-strlen(output)-1); }
void microkit_notify(int channel) { (void)channel; }
void microkit_irq_ack(int channel) { (void)channel; }
static void must(int ok, const char *why) { if(!ok) { fprintf(stderr,"FAIL: %s (%s)\n",why,output); exit(1); } }
int main(void) {
    console_mailbox_t m={0};
    console_mailbox_vaddr=(uintptr_t)&m;
    init();
    m.number=9; m.request_seq=1;
    notified(0);
    must(strstr(output,"[CONSOLE] native app printed: 9\n")!=NULL,"numeric service output");
    size_t length=strlen(output);
    notified(0);
    must(strlen(output)==length,"duplicate notification rejected");
    m.number=1234; m.request_seq=2;
    notified(0);
    must(strstr(output,"[CONSOLE] native app printed: 1234\n")!=NULL,"second service request");
    puts("PASS: console service processes bounded IPC requests and ignores duplicates");
    return 0;
}
