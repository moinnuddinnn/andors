/* Host integration test of REAL filesystem+terminal C source with event-driven
 * simulated seL4 notification delivery and a byte-addressable RAM block disk.
 * The actual VirtIO MMIO driver and QEMU timing are not covered by this test.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ring.h"
#include "fs_protocol.h"
#include "block_protocol.h"

extern uintptr_t fs_server_mailbox_vaddr;
extern uintptr_t fs_server_block_mailbox_vaddr;
extern uintptr_t fs_mailbox_vaddr;
extern uintptr_t rx_ring_vaddr;
extern uintptr_t tx_ring_vaddr;
extern void fs_server_init(void);
extern void fs_server_notified(int ch);
extern void terminal_init(void);
extern void terminal_notified(int ch);

static ring_t rx, tx;
static fs_mailbox_t fm;
static block_mailbox_t bm;
static uint8_t disk[128][BLOCK_SIZE];
static char output[4096];
static unsigned notify_counts[3];
static unsigned block_ops = 0;
static unsigned output_used;
static unsigned block_last_seq = 0;

enum { P_NONE = 0, P_FS = 1, P_TERM = 2 };
enum { E_FS = 1, E_TERM = 2, E_BLOCK = 3 };
static int current_pd = P_NONE;
typedef struct {int target;int ch;} event_t;
static event_t events[2048];
static unsigned ev_tail, ev_head;

static void check(bool condition, const char *message)
{
    if (!condition) {fprintf(stderr, "FAILED: %s\nlast output:\n%s\n", message, output);exit(1);}
}
void microkit_notify(int ch)
{
    event_t e;
    if (current_pd == P_FS) {
        e = (ch == 0) ? (event_t){E_TERM,1} : (event_t){E_BLOCK,0};
    } else if (current_pd == P_TERM) {
        if (ch == 0) return; /* UART output handled by drain() */
        e = (event_t){E_FS,0};
    } else {fprintf(stderr,"Unexpected callback from %d\n",current_pd);exit(1);}
    check(ev_head + 1 < sizeof(events)/sizeof(events[0]), "event queue overflow");
    events[ev_head++] = e;
    notify_counts[current_pd]++;
}
void microkit_dbg_puts(const char *str) {(void)str;}
void microkit_irq_ack(int ch) {(void)ch;}
static void run_block(void)
{
    uint32_t seq = bm.request_seq;
    if (seq == block_last_seq) return;
    block_last_seq = seq;
    ++block_ops;
    bm.status = BLOCK_OK;
    bm.capacity = 128u;
    if (bm.sector >= 128u) bm.status = BLOCK_ERR_RANGE;
    else if (bm.operation == BLOCK_READ) memcpy(bm.data,disk[bm.sector],BLOCK_SIZE);
    else if (bm.operation == BLOCK_WRITE) memcpy(disk[bm.sector],bm.data,BLOCK_SIZE);
    else bm.status = BLOCK_ERR_IO;
    bm.response_seq = seq;
    check(ev_head + 1 < sizeof(events)/sizeof(events[0]), "event queue overflow after block");
    events[ev_head++] = (event_t){E_FS,1};
}
static void pump(void)
{
    unsigned steps = 0;
    while (ev_tail != ev_head) {
        check(++steps < 1000, "runaway event loop");
        event_t e=events[ev_tail++];
        if (e.target == E_FS) {
            current_pd = P_FS;
            fs_server_notified(e.ch);
        } else if (e.target == E_TERM) {
            current_pd = P_TERM;
            terminal_notified(e.ch);
        } else if (e.target == E_BLOCK) {
            current_pd = P_NONE;
            run_block();
        }
    }
    ev_head = ev_tail = 0;
    current_pd = P_NONE;
}
static void drain(void)
{
    output_used=0;
    uint8_t b;
    while (ring_pop(&tx,&b)) {
        if(output_used+1 < sizeof(output)) output[output_used++] = (char)b;
    }
    output[output_used]='\0';
}
static void boot(void)
{
    memset(&rx,0,sizeof(rx));
    memset(&tx,0,sizeof(tx));
    memset(&fm,0,sizeof(fm));
    memset(&bm,0,sizeof(bm));
    memset(&events,0,sizeof(events));
    ev_head=ev_tail=0;
    block_last_seq=0;
    fs_server_mailbox_vaddr = (uintptr_t)&fm;
    fs_server_block_mailbox_vaddr = (uintptr_t)&bm;
    fs_mailbox_vaddr = (uintptr_t)&fm;
    rx_ring_vaddr = (uintptr_t)&rx;
    tx_ring_vaddr = (uintptr_t)&tx;
    current_pd=P_FS;
    fs_server_init();
    current_pd=P_TERM;
    terminal_init();
    pump();
    drain();
    check(strstr(output,"MICRO-OS v0.8")!=NULL,"boot banner v0.8");
}
static void command(const char *input,const char *expected)
{
    for(const char *p=input;*p;++p) check(ring_push(&rx,(uint8_t)*p),"rx push");
    check(ring_push(&rx,(uint8_t)'\r'),"rx enter");
    current_pd=P_TERM;
    terminal_notified(0);
    pump();
    drain();
    if(!strstr(output,expected)) {
        fprintf(stderr,"command `%s` expected `%s` got:\n%s\n",input,expected,output);
        exit(1);
    }
    check(strstr(output,"micro-os> ")!=NULL,"shell prompt after command");
}
int main(void)
{
    boot();
    command("fsinfo","UNSAVED changes");
    command("diskinfo","virtio-blk: available");
    command("mkdir projects","directory created");
    command("cd projects","/projects");
    command("write hello.txt Persisted across a reboot!","wrote 26 bytes");
    command("cat hello.txt","Persisted across a reboot!");
    /* Direct client call to FS load API; terminal is the sole FS client. */
    strcpy(fm.operation, "load");
    strcpy(fm.path, "hello.txt");
    ++fm.request_seq;
    current_pd=P_TERM; microkit_notify(1);
    pump();
    check(strstr(fm.reply, "OK:Persisted across a reboot!") != NULL,
          "machine-oriented program loading protocol");
    command("sync","Filesystem saved successfully (slot 0");
    command("fsinfo","No unsaved changes");
    check(disk[0][0]=='M' && disk[0][1]=='R',"slot A header committed");

    boot();
    command("cat /projects/hello.txt","Persisted across a reboot!");
    command("write /projects/hello.txt second snapshot","wrote 15 bytes");
    command("sync","Filesystem saved successfully (slot 1");
    boot();
    command("cat /projects/hello.txt","second snapshot");

    /* A payload corruption in the newest slot should fall back to slot A. */
    disk[34][100] ^= 0x7fu;
    boot();
    command("cat /projects/hello.txt","Persisted across a reboot!");
    command("diskinfo","Active slot 0");

    /* v0.8: exercising the long 216-character Base64 ELF write through the
     * actual UART parser -> filesystem -> snapshot -> reboot pathway. */
    command("mkdir /apps", "directory created");
    command("write /apps/square.elf f0VMRgIBAQAAAAAAAAAAAAMAtwABAAAAgAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAEAAOAABAAAAAAAAAAEAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAoAAAAAAAAACgAAAAAAAAABAAAAAAAAAAAAAAAAAAAADoAIBSACAAG8ADX9YAAAAAAAAAAAAAAAAAAAAAAAAAAA==", "wrote 216 bytes");
    command("sync", "Filesystem saved successfully");
    boot();
    command("ls /apps", "square.elf");
    command("cat /apps/square.elf", "f0VMRgIBAQ");

    check(block_ops >= 6u * 32u,"many independent sector transfers occurred");
    puts("PASS: filesystem/terminal IPC, disk commits, reboot restoration, and CRC fallback");
    return 0;
}
