/* Micro-OS v0.3: private in-memory hierarchical filesystem service.
 * This component has NO UART mapping. Its storage lives in its own BSS.
 * Requests/replies use one shared-memory page plus Microkit notifications.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "fs_protocol.h"

#define CH_TERMINAL 0
#define MAX_NODES 48
#define NAME_CAP 24
#define FILE_CAP 256

typedef struct {
    bool used;
    bool is_dir;
    int parent; /* root has parent 0 */
    unsigned size;
    char name[NAME_CAP];
    char contents[FILE_CAP];
} node_t;

uintptr_t fs_mailbox_vaddr = 0; /* patched by Microkit via setvar_vaddr */
static node_t nodes[MAX_NODES];
static int current_dir = 0;
static uint32_t last_request = 0;
static unsigned reply_used = 0;
static fs_mailbox_t *mailbox(void) { return (fs_mailbox_t *)fs_mailbox_vaddr; }

static unsigned string_len(const char *s)
{
    unsigned n = 0;
    while (s[n] != '\0') ++n;
    return n;
}
static bool equals(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0' && *a == *b) { ++a; ++b; }
    return *a == '\0' && *b == '\0';
}
static void copy_bounded(char *to, unsigned cap, const char *from)
{
    unsigned i = 0;
    while (i + 1u < cap && from[i] != '\0') { to[i] = from[i]; ++i; }
    if (cap > 0u) to[i] = '\0';
}
static void reply_reset(void)
{
    reply_used = 0;
    mailbox()->reply[0] = '\0';
}
static void say(const char *s)
{
    while (*s != '\0' && reply_used + 1u < FS_REPLY_LEN) {
        mailbox()->reply[reply_used++] = *s++;
    }
    mailbox()->reply[reply_used] = '\0';
}
static void number(unsigned x)
{
    char buf[12];
    unsigned i = 0;
    do { buf[i++] = (char)('0' + (x % 10u)); x /= 10u; } while (x);
    while (i != 0u) { char out[2] = {buf[--i], '\0'}; say(out); }
}
static int find_child(int parent, const char *name)
{
    for (int i = 1; i < MAX_NODES; ++i) {
        if (nodes[i].used && nodes[i].parent == parent && equals(nodes[i].name, name)) return i;
    }
    return -1;
}
static int new_node(int parent, const char *name, bool directory)
{
    for (int i = 1; i < MAX_NODES; ++i) {
        if (!nodes[i].used) {
            nodes[i].used = true;
            nodes[i].is_dir = directory;
            nodes[i].parent = parent;
            nodes[i].size = 0;
            nodes[i].contents[0] = '\0';
            copy_bounded(nodes[i].name, NAME_CAP, name);
            return i;
        }
    }
    return -1;
}
/* Resolve absolute and relative paths, including . and .., without libc. */
static int resolve(const char *path)
{
    int at = (path[0] == '/') ? 0 : current_dir;
    unsigned pos = 0;
    while (path[pos] != '\0') {
        while (path[pos] == '/') ++pos;
        if (path[pos] == '\0') break;
        char part[NAME_CAP];
        unsigned n = 0;
        while (path[pos] != '/' && path[pos] != '\0') {
            if (n + 1u >= NAME_CAP) return -1;
            part[n++] = path[pos++];
        }
        part[n] = '\0';
        if (equals(part, ".")) continue;
        if (equals(part, "..")) { at = nodes[at].parent; continue; }
        if (!nodes[at].is_dir) return -1;
        at = find_child(at, part);
        if (at < 0) return -1;
    }
    return at;
}
/* Split new object's path into its parent directory and final component. */
static bool parent_and_name(const char *path, int *parent, char name[NAME_CAP])
{
    unsigned len = string_len(path);
    if (len == 0u || len >= FS_PATH_LEN || path[len - 1u] == '/') return false;
    unsigned slash = len;
    while (slash != 0u && path[slash - 1u] != '/') --slash;
    unsigned leaf_len = len - slash;
    if (leaf_len == 0u || leaf_len >= NAME_CAP) return false;
    for (unsigned i = 0; i < leaf_len; ++i) name[i] = path[slash + i];
    name[leaf_len] = '\0';
    if (equals(name, ".") || equals(name, "..")) return false;
    if (slash == 0u) {
        *parent = current_dir;
    } else if (slash == 1u) {
        *parent = 0;
    } else {
        char directory[FS_PATH_LEN];
        for (unsigned i = 0; i < slash - 1u; ++i) directory[i] = path[i];
        directory[slash - 1u] = '\0';
        *parent = resolve(directory);
    }
    return *parent >= 0 && nodes[*parent].is_dir;
}
static void cmd_pwd(void)
{
    if (current_dir == 0) { say("/\n"); return; }
    int chain[MAX_NODES];
    unsigned length = 0;
    int at = current_dir;
    while (at != 0 && length < MAX_NODES) {
        chain[length++] = at;
        at = nodes[at].parent;
    }
    while (length != 0u) { say("/"); say(nodes[chain[--length]].name); }
    say("\n");
}
static void cmd_ls(const char *path)
{
    int dir = resolve(path);
    if (dir < 0) { say("ls: path not found\n"); return; }
    if (!nodes[dir].is_dir) { say(nodes[dir].name); say("\n"); return; }
    unsigned found = 0;
    for (int i = 1; i < MAX_NODES; ++i) {
        if (nodes[i].used && nodes[i].parent == dir) {
            if (reply_used > FS_REPLY_LEN - 85u) {
                say("... list truncated\n");
                break;
            }
            say(nodes[i].is_dir ? "d  " : "f  ");
            say(nodes[i].name);
            if (nodes[i].is_dir) say("/");
            else { say("  ("); number(nodes[i].size); say(" bytes)"); }
            say("\n");
            ++found;
        }
    }
    if (found == 0u) say("(empty)\n");
}
static void cmd_create(const char *path, bool directory)
{
    int parent;
    char name[NAME_CAP];
    if (!parent_and_name(path, &parent, name)) { say("invalid path or name (max 23 chars)\n"); return; }
    if (find_child(parent, name) >= 0) { say("already exists\n"); return; }
    if (new_node(parent, name, directory) < 0) { say("filesystem full (no free entries)\n"); return; }
    say(directory ? "directory created\n" : "file created\n");
}
static void cmd_cd(const char *path)
{
    int to = resolve(path[0] == '\0' ? "/" : path);
    if (to < 0) { say("cd: path not found\n"); return; }
    if (!nodes[to].is_dir) { say("cd: not a directory\n"); return; }
    current_dir = to;
    cmd_pwd();
}
static void cmd_cat(const char *path)
{
    int file = resolve(path);
    if (file < 0) { say("cat: file not found\n"); return; }
    if (nodes[file].is_dir) { say("cat: is a directory\n"); return; }
    say(nodes[file].contents);
    say("\n");
}
static void cmd_write(const char *path, const char *data, bool append)
{
    int file = resolve(path);
    if (file < 0) {
        int parent;
        char name[NAME_CAP];
        if (!parent_and_name(path, &parent, name)) { say("write: invalid path\n"); return; }
        file = new_node(parent, name, false);
        if (file < 0) { say("filesystem full\n"); return; }
    }
    if (nodes[file].is_dir) { say("write: cannot write a directory\n"); return; }
    unsigned extra = string_len(data);
    unsigned from = append ? nodes[file].size : 0u;
    if (from + extra >= FILE_CAP) { say("file too large (maximum 255 bytes)\n"); return; }
    for (unsigned i = 0; i < extra; ++i) nodes[file].contents[from + i] = data[i];
    nodes[file].size = from + extra;
    nodes[file].contents[nodes[file].size] = '\0';
    say("wrote "); number(extra); say(" bytes\n");
}
static void cmd_remove(const char *path, bool want_directory)
{
    int target = resolve(path);
    if (target < 0) { say("path not found\n"); return; }
    if (target == 0) { say("cannot remove filesystem root\n"); return; }
    if (nodes[target].is_dir != want_directory) {
        say(want_directory ? "rmdir: not a directory\n" : "rm: is a directory\n");
        return;
    }
    if (want_directory) {
        if (target == current_dir) { say("rmdir: cannot remove working directory\n"); return; }
        for (int i = 1; i < MAX_NODES; ++i) {
            if (nodes[i].used && nodes[i].parent == target) {
                say("rmdir: directory not empty\n"); return;
            }
        }
    }
    nodes[target].used = false;
    say("removed\n");
}
static void cmd_fsinfo(void)
{
    unsigned files = 0, dirs = 0, bytes = 0;
    for (int i = 0; i < MAX_NODES; ++i) {
        if (nodes[i].used) {
            if (nodes[i].is_dir) ++dirs;
            else { ++files; bytes += nodes[i].size; }
        }
    }
    say("RAMFS: "); number(files); say(" files, "); number(dirs);
    say(" directories, "); number(bytes); say(" data bytes\n");
    say("Capacity: "); number(MAX_NODES); say(" entries, ");
    number(FILE_CAP - 1u); say(" bytes per file. Volatile: lost after reboot.\n");
}
static void dispatch(void)
{
    fs_mailbox_t *m = mailbox();
    reply_reset();
    if (equals(m->operation, "ls")) cmd_ls(m->path);
    else if (equals(m->operation, "pwd")) cmd_pwd();
    else if (equals(m->operation, "mkdir")) cmd_create(m->path, true);
    else if (equals(m->operation, "touch")) cmd_create(m->path, false);
    else if (equals(m->operation, "cd")) cmd_cd(m->path);
    else if (equals(m->operation, "cat")) cmd_cat(m->path);
    else if (equals(m->operation, "write")) cmd_write(m->path, m->data, false);
    else if (equals(m->operation, "append")) cmd_write(m->path, m->data, true);
    else if (equals(m->operation, "rm")) cmd_remove(m->path, false);
    else if (equals(m->operation, "rmdir")) cmd_remove(m->path, true);
    else if (equals(m->operation, "fsinfo")) cmd_fsinfo();
    else say("filesystem: unknown operation\n");
}
void init(void)
{
    nodes[0].used = true;
    nodes[0].is_dir = true;
    nodes[0].parent = 0;
    nodes[0].name[0] = '\0';
    int home = new_node(0, "home", true);
    int sys = new_node(0, "system", true);
    if (home > 0) {
        int f = new_node(home, "readme.txt", false);
        if (f > 0) {
            const char *text = "Welcome to Micro-OS RAMFS. Files disappear when QEMU exits.";
            copy_bounded(nodes[f].contents, FILE_CAP, text);
            nodes[f].size = string_len(nodes[f].contents);
        }
    }
    if (sys > 0) {
        int f = new_node(sys, "version.txt", false);
        if (f > 0) {
            const char *text = "Micro-OS v0.3 / seL4 Microkit 2.4.0";
            copy_bounded(nodes[f].contents, FILE_CAP, text);
            nodes[f].size = string_len(nodes[f].contents);
        }
    }
    microkit_dbg_puts("[FS] RAM filesystem server online (separate PD)\n");
}
void notified(microkit_channel channel)
{
    if (channel != CH_TERMINAL) return;
    fs_mailbox_t *m = mailbox();
    uint32_t seq = __atomic_load_n(&m->request_seq, __ATOMIC_ACQUIRE);
    if (seq == last_request) return; /* notifications may coalesce */
    last_request = seq;
    dispatch();
    __atomic_store_n(&m->response_seq, seq, __ATOMIC_RELEASE);
    microkit_notify(CH_TERMINAL);
}
