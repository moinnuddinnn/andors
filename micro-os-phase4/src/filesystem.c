/* Micro-OS v0.4: private hierarchical filesystem + crash-recoverable snapshots.
 * Files live in this PD's RAM; sync copies a whole snapshot to a VirtIO block
 * driver PD using a separate one-page mailbox and notifications.
 * Two alternating on-disk slots; payload first, checksum header last.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "fs_protocol.h"
#include "block_protocol.h"

#define CH_TERMINAL 0
#define CH_BLOCK 1
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
typedef struct {
    node_t nodes[MAX_NODES];
    int current_dir;
} fs_state_t;

/* Two slots, each 1 header sector + 32 payload sectors = 66 sectors. */
#define PAYLOAD_SECTORS 32u
#define SLOT_SECTORS (PAYLOAD_SECTORS + 1u)
#define DISK_MAGIC 0x534F524Du  /* 'MROS' stored little-endian */
#define DISK_VERSION 1u
_Static_assert(sizeof(fs_state_t) <= PAYLOAD_SECTORS * BLOCK_SIZE,
               "filesystem state too large for on-disk snapshot");

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    uint32_t byte_length;
    uint32_t crc32;
} disk_header_t;

static fs_state_t live_state;
#define nodes live_state.nodes
#define current_dir live_state.current_dir

uintptr_t block_mailbox_vaddr = 0; /* setvar_vaddr from system.xml */
static bool dirty = false;
static bool disk_available = true;
static bool fs_ready = false;
static int active_slot = -1;
static uint32_t generation = 0;
static uint32_t block_seq = 0;
static uint32_t handled_block_seq = 0;
static disk_header_t headers[2];
static unsigned loading_slot = 0;
static unsigned saving_slot = 0;
static unsigned transfer_index = 0;
static disk_header_t pending_header;

typedef enum {
    BOOT_HEADER_0, BOOT_HEADER_1, BOOT_PAYLOAD, READY,
    SAVE_PAYLOAD, SAVE_HEADER
} fs_phase_t;
static fs_phase_t phase = BOOT_HEADER_0;

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
            dirty = true;
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
    dirty = true;
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
    dirty = true;
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
    dirty = true;
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
    say("MicroFS: "); number(files); say(" files, "); number(dirs);
    say(" directories, "); number(bytes); say(" data bytes\n");
    say("Capacity: "); number(MAX_NODES); say(" entries, ");
    number(FILE_CAP - 1u); say(" bytes per file. ");
    say(dirty ? "UNSAVED changes (run sync).\n" : "No unsaved changes.\n");
}

static void copy_bytes(void *dst, const void *src, unsigned count)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (unsigned i = 0; i < count; ++i) d[i] = s[i];
}
static void zero_bytes(void *dst, unsigned count)
{
    uint8_t *d = (uint8_t *)dst;
    for (unsigned i = 0; i < count; ++i) d[i] = 0;
}
/* CRC32 of raw AArch64 C layout, protected by format version/size. */
static uint32_t crc32_state(void)
{
    const uint8_t *src = (const uint8_t *)&live_state;
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < sizeof(live_state); ++i) {
        crc ^= src[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    return ~crc;
}
static bool header_valid(const disk_header_t *h)
{
    return h->magic == DISK_MAGIC && h->version == DISK_VERSION &&
           h->byte_length == sizeof(live_state) && h->generation != 0u;
}
static bool has_nul(const char *buf, unsigned cap)
{
    for (unsigned i = 0; i < cap; ++i) if (buf[i] == '\0') return true;
    return false;
}
static bool state_valid(void)
{
    if (!nodes[0].used || !nodes[0].is_dir || nodes[0].parent != 0) return false;
    if (current_dir < 0 || current_dir >= MAX_NODES ||
        !nodes[current_dir].used || !nodes[current_dir].is_dir) return false;
    for (int i = 1; i < MAX_NODES; ++i) {
        if (!nodes[i].used) continue;
        if (nodes[i].parent < 0 || nodes[i].parent >= MAX_NODES ||
            !nodes[nodes[i].parent].used || !nodes[nodes[i].parent].is_dir ||
            !has_nul(nodes[i].name, NAME_CAP) ||
            nodes[i].size >= FILE_CAP ||
            (!nodes[i].is_dir && nodes[i].contents[nodes[i].size] != '\0')) return false;
    }
    return true;
}
static block_mailbox_t *block(void)
{
    return (block_mailbox_t *)block_mailbox_vaddr;
}
static void send_sector(uint32_t op, uint64_t sector, const uint8_t *bytes)
{
    block_mailbox_t *b = block();
    b->operation = op;
    b->sector = sector;
    if (bytes != 0) copy_bytes(b->data, bytes, BLOCK_SIZE);
    ++block_seq;
    __atomic_store_n(&b->request_seq, block_seq, __ATOMIC_RELEASE);
    microkit_notify(CH_BLOCK);
}
static void send_boot_payload(void)
{
    send_sector(BLOCK_READ, (uint64_t)loading_slot * SLOT_SECTORS +
                1u + transfer_index, 0);
}
static void send_save_payload(void)
{
    uint8_t sector[BLOCK_SIZE];
    zero_bytes(sector, BLOCK_SIZE);
    unsigned offset = transfer_index * BLOCK_SIZE;
    if (offset < sizeof(live_state)) {
        unsigned count = (unsigned)sizeof(live_state) - offset;
        if (count > BLOCK_SIZE) count = BLOCK_SIZE;
        copy_bytes(sector, (uint8_t *)&live_state + offset, count);
    }
    send_sector(BLOCK_WRITE, (uint64_t)saving_slot * SLOT_SECTORS +
                1u + transfer_index, sector);
}
static void seed_filesystem(void)
{
    zero_bytes(&live_state, sizeof(live_state));
    nodes[0].used = true;
    nodes[0].is_dir = true;
    nodes[0].parent = 0;
    int home = new_node(0, "home", true);
    int sys = new_node(0, "system", true);
    if (home > 0) {
        int f = new_node(home, "readme.txt", false);
        if (f > 0) {
            const char *text = "Welcome to Micro-OS v0.4. Run sync to save on disk!";
            copy_bounded(nodes[f].contents, FILE_CAP, text);
            nodes[f].size = string_len(nodes[f].contents);
        }
    }
    if (sys > 0) {
        int f = new_node(sys, "version.txt", false);
        if (f > 0) {
            const char *text = "Micro-OS v0.4 / seL4 Microkit 2.4.0";
            copy_bounded(nodes[f].contents, FILE_CAP, text);
            nodes[f].size = string_len(nodes[f].contents);
        }
    }
    dirty = true;
    active_slot = -1;
    generation = 0;
}
static void service_pending_terminal(void);
static void finish_boot(bool loaded)
{
    if (!loaded) {
        seed_filesystem();
        microkit_dbg_puts("[FS] No valid disk snapshot, using fresh filesystem\n");
    } else {
        dirty = false;
        microkit_dbg_puts("[FS] Restored filesystem from persistent disk\n");
    }
    fs_ready = true;
    phase = READY;
    service_pending_terminal();
}
static void select_boot_slot(void)
{
    bool a = header_valid(&headers[0]);
    bool b = header_valid(&headers[1]);
    if (!a && !b) { finish_boot(false); return; }
    loading_slot = a && (!b || headers[0].generation >= headers[1].generation)
                    ? 0u : 1u;
    transfer_index = 0;
    phase = BOOT_PAYLOAD;
    send_boot_payload();
}
static void complete_terminal_request(void)
{
    __atomic_store_n(&mailbox()->response_seq, last_request, __ATOMIC_RELEASE);
    microkit_notify(CH_TERMINAL);
}
static void fail_save(void)
{
    disk_available = false;
    phase = READY;
    reply_reset();
    say("sync failed: block I/O error. Changes remain in RAM.\n");
    complete_terminal_request();
}
static void on_block_response(void)
{
    block_mailbox_t *b = block();
    uint32_t seq = __atomic_load_n(&b->response_seq, __ATOMIC_ACQUIRE);
    if (seq == handled_block_seq || seq != block_seq) return;
    handled_block_seq = seq;
    if (b->status != BLOCK_OK) {
        if (phase == BOOT_HEADER_0 || phase == BOOT_HEADER_1 || phase == BOOT_PAYLOAD) {
            disk_available = false;
            finish_boot(false);
        } else if (phase == SAVE_PAYLOAD || phase == SAVE_HEADER) {
            fail_save();
        }
        return;
    }
    if (phase == BOOT_HEADER_0) {
        copy_bytes(&headers[0], b->data, sizeof(disk_header_t));
        phase = BOOT_HEADER_1;
        send_sector(BLOCK_READ, SLOT_SECTORS, 0);
    } else if (phase == BOOT_HEADER_1) {
        copy_bytes(&headers[1], b->data, sizeof(disk_header_t));
        select_boot_slot();
    } else if (phase == BOOT_PAYLOAD) {
        unsigned offset = transfer_index * BLOCK_SIZE;
        if (offset < sizeof(live_state)) {
            unsigned count = (unsigned)sizeof(live_state) - offset;
            if (count > BLOCK_SIZE) count = BLOCK_SIZE;
            copy_bytes((uint8_t *)&live_state + offset, b->data, count);
        }
        if (++transfer_index < PAYLOAD_SECTORS) {
            send_boot_payload();
        } else if (state_valid() && crc32_state() == headers[loading_slot].crc32) {
            active_slot = (int)loading_slot;
            generation = headers[loading_slot].generation;
            finish_boot(true);
        } else {
            /* Try previous valid checkpoint if latest was torn/corrupted. */
            headers[loading_slot].magic = 0;
            select_boot_slot();
        }
    } else if (phase == SAVE_PAYLOAD) {
        if (++transfer_index < PAYLOAD_SECTORS) {
            send_save_payload();
        } else {
            uint8_t hdr[BLOCK_SIZE];
            zero_bytes(hdr, BLOCK_SIZE);
            copy_bytes(hdr, &pending_header, sizeof(pending_header));
            phase = SAVE_HEADER;
            send_sector(BLOCK_WRITE, (uint64_t)saving_slot * SLOT_SECTORS, hdr);
        }
    } else if (phase == SAVE_HEADER) {
        active_slot = (int)saving_slot;
        generation = pending_header.generation;
        dirty = false;
        phase = READY;
        reply_reset();
        say("Filesystem saved successfully (slot ");
        number(saving_slot);
        say(", generation ");
        number(generation);
        say(").\n");
        complete_terminal_request();
    }
}
static void cmd_diskinfo(void)
{
    if (!disk_available) {
        say("virtio-blk: unavailable (RAM-only mode)\n");
        return;
    }
    say("virtio-blk: available, ");
    number((unsigned)block()->capacity);
    say(" sectors of 512 bytes. Checkpoint generation ");
    number(generation);
    say(". Active slot ");
    if (active_slot < 0) say("none");
    else number((unsigned)active_slot);
    say(".\n");
}
static void begin_sync(void)
{
    if (!disk_available) {
        say("sync unavailable: no working VirtIO block device.\n");
        return;
    }
    saving_slot = (active_slot == 0) ? 1u : 0u;
    transfer_index = 0;
    pending_header.magic = DISK_MAGIC;
    pending_header.version = DISK_VERSION;
    pending_header.generation = generation + 1u;
    if (pending_header.generation == 0u) pending_header.generation = 1u;
    pending_header.byte_length = sizeof(live_state);
    pending_header.crc32 = crc32_state();
    phase = SAVE_PAYLOAD;
    send_save_payload();
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
    else if (equals(m->operation, "diskinfo")) cmd_diskinfo();
    else if (equals(m->operation, "sync")) begin_sync();
    else say("filesystem: unknown operation\n");
}
static void service_pending_terminal(void)
{
    if (!fs_ready || phase != READY) return;
    uint32_t seq = __atomic_load_n(&mailbox()->request_seq, __ATOMIC_ACQUIRE);
    if (seq == last_request) return; /* duplicate/coalesced notification */
    last_request = seq;
    dispatch();
    if (phase == READY) complete_terminal_request();
}
void init(void)
{
    /* A real reboot naturally zeros BSS; resetting here also enables host tests. */
    zero_bytes(&live_state, sizeof(live_state));
    zero_bytes(&headers, sizeof(headers));
    dirty = false;
    disk_available = true;
    fs_ready = false;
    active_slot = -1;
    generation = 0;
    block_seq = 0;
    handled_block_seq = 0;
    last_request = 0;
    phase = BOOT_HEADER_0;
    microkit_dbg_puts("[FS] MicroFS service starting, looking for saved snapshots\n");
    send_sector(BLOCK_READ, 0u, 0);
}
void notified(microkit_channel channel)
{
    if (channel == CH_TERMINAL) service_pending_terminal();
    else if (channel == CH_BLOCK) on_block_response();
}
