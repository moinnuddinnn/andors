/* Host-side event simulation of real v0.5 terminal, manager, and two app PDs.
 * Each component is compiled from its production .c file with renamed entry
 * points. This does not emulate kernel protection or replace a QEMU boot test.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ring.h"
#include "app_protocol.h"

extern uintptr_t rx_ring_vaddr, tx_ring_vaddr, app_mailbox_vaddr;
extern uintptr_t manager_app_mailbox_vaddr, manager_calc_mailbox_vaddr;
extern uintptr_t manager_counter_mailbox_vaddr;
extern uintptr_t calc_worker_mailbox_vaddr, counter_worker_mailbox_vaddr;
extern void terminal_init(void), terminal_notified(int);
extern void manager_init(void), manager_notified(int);
extern void calc_init(void), calc_notified(int);
extern void counter_init(void), counter_notified(int);

static ring_t rx, tx;
static app_mailbox_t client_mailbox;
static app_worker_mailbox_t calc_mailbox, counter_mailbox;
static char output[4096];
static enum { NONE, TERM, MANAGER, CALC, COUNTER } sender;
typedef struct { int target, channel; } event_t;
static event_t events[256];
static unsigned head = 0, tail = 0;
static unsigned notifications = 0;

static void check(bool pass, const char *message)
{
    if (!pass) {
        fprintf(stderr, "FAIL: %s (output = %s)\n", message, output);
        exit(1);
    }
}
void microkit_notify(int channel)
{
    event_t e = {0, 0};
    if (sender == TERM) {
        if (channel == 0 || channel == 1) return; /* UART/FS separate */
        if (channel == 2) e = (event_t){MANAGER,0};
    } else if (sender == MANAGER) {
        if (channel == 0) e = (event_t){TERM,2};
        if (channel == 1) e = (event_t){CALC,0};
        if (channel == 2) e = (event_t){COUNTER,0};
    } else if (sender == CALC && channel == 0) e = (event_t){MANAGER,1};
    else if (sender == COUNTER && channel == 0) e = (event_t){MANAGER,2};
    check(e.target != 0, "unknown IPC channel/sender");
    check(head < sizeof(events)/sizeof(events[0]), "event queue overflow");
    events[head++] = e;
    ++notifications;
}
void microkit_dbg_puts(const char *s) { (void)s; }
void microkit_irq_ack(int c) { (void)c; }
static void pump(void)
{
    int n = 0;
    while (tail < head) {
        check(++n < 256, "notification loop");
        event_t e = events[tail++];
        if (e.target == TERM) { sender = TERM; terminal_notified(e.channel); }
        else if (e.target == MANAGER) { sender = MANAGER; manager_notified(e.channel); }
        else if (e.target == CALC) { sender = CALC; calc_notified(e.channel); }
        else if (e.target == COUNTER) { sender = COUNTER; counter_notified(e.channel); }
    }
    tail = head = 0;
    sender = NONE;
}
static void drain(void)
{
    unsigned n = 0;
    uint8_t b;
    while (ring_pop(&tx,&b)) if (n + 1 < sizeof(output)) output[n++] = (char)b;
    output[n] = '\0';
}
static void boot(void)
{
    memset(&rx,0,sizeof(rx)); memset(&tx,0,sizeof(tx));
    memset(&client_mailbox,0,sizeof(client_mailbox));
    memset(&calc_mailbox,0,sizeof(calc_mailbox));
    memset(&counter_mailbox,0,sizeof(counter_mailbox));
    app_mailbox_vaddr = manager_app_mailbox_vaddr = (uintptr_t)&client_mailbox;
    manager_calc_mailbox_vaddr = calc_worker_mailbox_vaddr = (uintptr_t)&calc_mailbox;
    manager_counter_mailbox_vaddr = counter_worker_mailbox_vaddr = (uintptr_t)&counter_mailbox;
    rx_ring_vaddr = (uintptr_t)&rx;
    tx_ring_vaddr = (uintptr_t)&tx;
    sender = CALC; calc_init();
    sender = COUNTER; counter_init();
    sender = MANAGER; manager_init();
    sender = TERM; terminal_init();
    pump(); drain();
    check(strstr(output, "MICRO-OS v0.5")!=NULL, "boot banner");
}
static void command(const char *command, const char *expect)
{
    for (const char *p = command; *p; ++p)
        check(ring_push(&rx, (uint8_t)*p), "RX ring full");
    check(ring_push(&rx, '\r'), "RX ring enter");
    sender = TERM; terminal_notified(0);
    pump(); drain();
    if (!strstr(output,expect)) {
        fprintf(stderr,"Command '%s' expected '%s', got:\n%s\n",command,expect,output);
        exit(1);
    }
    check(strstr(output,"micro-os> ")!=NULL,"prompt returns");
}
int main(void)
{
    boot();
    command("apps", "Available isolated C applications");
    command("run calc 7 + 5", "calculator: 7 + 5 = 12");
    command("run calculator 3 + 9", "calculator: 3 + 9 = 12");
    command("run calc -12 * 4", "calculator: -12 * 4 = -48");
    command("run calc 8 / 0", "division by zero");
    command("run calc 7.2 + 2", "Usage: run calc");
    command("run counter 5", "counter: 1 2 3 4 5");
    command("run counter 40", "Usage: run counter");
    command("run garbage", "Unknown app");
    command("ps", "calculator      DONE");
    check(strstr(output,"counter         DONE") != NULL, "counter process shown");
    check(strstr(output,"       5") != NULL, "calculator job count");
    command("help", "run counter 8");
    command("version", "Micro-OS v0.5");
    check(notifications > 15, "all notification paths exercised");
    puts("PASS: real shell->manager->isolated-app IPC and sequence-based responses");
    return 0;
}
