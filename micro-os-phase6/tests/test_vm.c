#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_protocol.h"
extern uintptr_t script_mailbox_vaddr;
extern void init(void), notified(int);
static app_worker_mailbox_t mb;
static int notifications;
void microkit_notify(int c) { if (c != 0) exit(1); ++notifications; }
void microkit_dbg_puts(const char *s) { (void)s; }
void microkit_irq_ack(int c) { (void)c; }
static void run(const char *script, const char *expected)
{
    strcpy(mb.args, script);
    ++mb.request_seq;
    notified(0);
    if (!strstr(mb.reply, expected)) {
        fprintf(stderr,"script %s expected %s got %s\n", script,expected,mb.reply);
        exit(1);
    }
    if (mb.response_seq != mb.request_seq) exit(1);
}
int main(void)
{
    script_mailbox_vaddr=(uintptr_t)&mb;
    init();
    run("print From disk; add 7 5; count 3; end", "7 + 5 = 12");
    run("print Done", "Done");
    run("add 8 -3", "8 + -3 = 5");
    run("count 0", "expects 1..20");
    run("add x 3", "expects two integers");
    run("nonsense", "unknown statement");
    run("", "1..255 bytes");
    int previous=notifications;
    notified(0);
    if(notifications!=previous) exit(1);
    puts("PASS: bounded isolated script interpreter and request sequencing");
    return 0;
}
