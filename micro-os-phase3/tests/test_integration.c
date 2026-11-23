/* Host integration simulation: real shell + filesystem code sharing mailbox.
 * Does not emulate seL4, UART MMIO, interrupts or QEMU timing.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ring.h"
#include "fs_protocol.h"

extern uintptr_t fs_server_mailbox_vaddr;
extern uintptr_t fs_mailbox_vaddr;
extern uintptr_t rx_ring_vaddr;
extern uintptr_t tx_ring_vaddr;
extern void fs_server_init(void);
extern void fs_server_notified(int ch);
extern void terminal_init(void);
extern void terminal_notified(int ch);

static ring_t rx, tx;
static fs_mailbox_t mailbox_storage;
static char output[4096];
void microkit_notify(int channel) { (void)channel; }
void microkit_dbg_puts(const char *message) { (void)message; }
void microkit_irq_ack(int channel) { (void)channel; }
static void drain(void)
{
    unsigned n = 0;
    uint8_t b;
    while (ring_pop(&tx, &b)) {
        if (n + 1 < sizeof(output)) output[n++] = (char)b;
    }
    output[n] = '\0';
}
static void command(const char *s, const char *expect)
{
    for (const char *p = s; *p; ++p) assert(ring_push(&rx, (uint8_t)*p));
    assert(ring_push(&rx, (uint8_t)'\r'));
    terminal_notified(0);
    /* Even local commands have no pending request; calling the filesystem
     * with the same sequence intentionally tests duplicate-notify handling. */
    fs_server_notified(0);
    terminal_notified(1);
    drain();
    if (!strstr(output, expect)) {
        fprintf(stderr, "cmd: %s; missing: %s; output: %s\n", s, expect, output);
        assert(0);
    }
    assert(strstr(output, "micro-os> "));
}
int main(void)
{
    fs_server_mailbox_vaddr = (uintptr_t)&mailbox_storage;
    fs_mailbox_vaddr = (uintptr_t)&mailbox_storage;
    rx_ring_vaddr = (uintptr_t)&rx;
    tx_ring_vaddr = (uintptr_t)&tx;
    fs_server_init();
    terminal_init();
    drain();
    assert(strstr(output, "MICRO-OS v0.3"));
    command("ls", "home/");
    command("pwd", "/\r\n");
    command("mkdir projects", "directory created");
    command("cd projects", "/projects");
    command("touch hello.txt", "file created");
    command("write hello.txt Microkernel powered!", "wrote 20 bytes");
    command("cat hello.txt", "Microkernel powered!");
    command("append hello.txt And more.", "wrote 9 bytes");
    command("cat hello.txt", "Microkernel powered!And more.");
    command("ls", "hello.txt");
    command("rm hello.txt", "removed");
    command("cd ..", "/\r\n");
    command("rmdir projects", "removed");
    command("help", "fsinfo");
    command("about", "three isolated protection domains");
    command("version", "Micro-OS v0.3");
    puts("PASS: terminal-to-RAMFS command flow over shared mailbox and notifications");
    return 0;
}
