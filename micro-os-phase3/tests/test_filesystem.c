#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fs_protocol.h"

extern uintptr_t fs_mailbox_vaddr;
extern void init(void);
extern void notified(int channel);

static fs_mailbox_t state;
static unsigned notify_count;

void microkit_notify(int channel) { assert(channel == 0); ++notify_count; }
void microkit_dbg_puts(const char *message) { (void)message; }
void microkit_irq_ack(int channel) { (void)channel; }

static const char *run(const char *op, const char *path, const char *data)
{
    memset(state.operation, 0, sizeof(state.operation));
    memset(state.path, 0, sizeof(state.path));
    memset(state.data, 0, sizeof(state.data));
    snprintf(state.operation, sizeof(state.operation), "%s", op);
    snprintf(state.path, sizeof(state.path), "%s", path);
    snprintf(state.data, sizeof(state.data), "%s", data);
    state.request_seq++;
    unsigned before = notify_count;
    notified(0);
    assert(state.response_seq == state.request_seq);
    assert(notify_count == before + 1);
    assert(strnlen(state.reply, sizeof(state.reply)) < sizeof(state.reply));
    return state.reply;
}
static void contains(const char *s, const char *piece)
{
    if (!strstr(s, piece)) {
        fprintf(stderr, "Expected to find '%s' in '%s'\n", piece, s);
        assert(0);
    }
}
int main(void)
{
    fs_mailbox_vaddr = (uintptr_t)&state;
    init();
    contains(run("pwd", "", ""), "/\n");
    contains(run("ls", "/", ""), "home/");
    contains(run("ls", "/", ""), "system/");
    contains(run("cat", "/home/readme.txt", ""), "disappear");
    contains(run("cd", "/home", ""), "/home\n");
    contains(run("mkdir", "projects", ""), "created");
    contains(run("cd", "projects", ""), "/home/projects\n");
    contains(run("touch", "hello.txt", ""), "file created");
    contains(run("write", "hello.txt", "Hello from Micro-OS!"), "wrote 20 bytes");
    contains(run("cat", "./hello.txt", ""), "Hello from Micro-OS!");
    contains(run("append", "hello.txt", " Hello again"), "wrote 12 bytes");
    contains(run("cat", "hello.txt", ""), "Hello from Micro-OS! Hello again");
    contains(run("ls", "..", ""), "projects/");
    contains(run("rm", "hello.txt", ""), "removed");
    contains(run("cat", "hello.txt", ""), "not found");
    contains(run("cd", "..", ""), "/home\n");
    contains(run("rmdir", "projects", ""), "removed");
    contains(run("cd", "/", ""), "/\n");
    contains(run("rmdir", "/home", ""), "not empty");
    contains(run("write", "/home/notes.txt", "Hello world"), "wrote 11 bytes");
    contains(run("cat", "/home/notes.txt", ""), "Hello world");
    contains(run("mkdir", "/home/notes.txt", ""), "already exists");
    contains(run("fsinfo", "", ""), "RAMFS:");
    contains(run("cat", "../system/version.txt", ""), "Micro-OS v0.3");
    contains(run("write", "/home/readme.txt", "replacement"), "wrote 11 bytes");
    contains(run("cat", "/home/readme.txt", ""), "replacement\n");
    /* Spurious/coalesced notifications cannot repeat commands. */
    unsigned count = notify_count;
    notified(0);
    assert(notify_count == count);
    puts("PASS: RAMFS paths, file operations, limits, and notifications");
    return 0;
}
