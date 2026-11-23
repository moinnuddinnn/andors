/* Micro-OS v0.8: interactive shell with independent UART and filesystem PDs.
 * Filesystem commands are sent asynchronously through a shared mailbox.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "ring.h"
#include "fs_protocol.h"
#include "app_protocol.h"
#include "elf_loader.h"

#define CH_UART_DRIVER 0
#define CH_FILESYSTEM  1
#define CH_APPMANAGER  2
#define MAX_LINE       320u

/* Addresses patched by the Microkit image builder. */
uintptr_t rx_ring_vaddr = 0;
uintptr_t tx_ring_vaddr = 0;
uintptr_t fs_mailbox_vaddr = 0;
uintptr_t app_mailbox_vaddr = 0;
uintptr_t native_code_vaddr = 0;

static char line[MAX_LINE];
static unsigned line_length = 0;
static bool last_was_cr = false;
static bool waiting_fs = false;
static bool waiting_app = false;
static bool waiting_exec_fs = false;
static bool waiting_native_fs = false;
static unsigned native_input = 0;
static uint32_t awaiting_sequence = 0;
static uint32_t awaiting_app_sequence = 0;

static void emit_char(char c)
{
    /* Output that exceeds the 2-KiB ring can be dropped in this prototype. */
    (void)ring_push((ring_t *)tx_ring_vaddr, (uint8_t)c);
}
static void emit(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') emit_char('\r');
        emit_char(*s++);
    }
}
static bool equals(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == '\0' && *b == '\0';
}
static bool starts_with(const char *a, const char *prefix)
{
    while (*prefix) { if (*a++ != *prefix++) return false; }
    return true;
}
static void copy_bounded(char *out, unsigned cap, const char *src)
{
    unsigned i = 0;
    while (i + 1u < cap && src[i] != '\0') { out[i] = src[i]; ++i; }
    if (cap > 0) out[i] = '\0';
}
static unsigned string_len(const char *s)
{
    unsigned i = 0;
    while (s[i]) ++i;
    return i;
}
static void prompt(void) { emit("micro-os> "); }

/* Split a command into operation, path, and optional free-form content.
 * Paths cannot contain spaces. The content of 'write'/'append' may contain spaces.
 */
static bool parse_fs_command(const char *input, char op[FS_OP_LEN],
                             char path[FS_PATH_LEN], char data[FS_REQUEST_LEN])
{
    unsigned idx = 0, n = 0;
    while (input[idx] == ' ') ++idx;
    while (input[idx] && input[idx] != ' ') {
        if (n + 1u >= FS_OP_LEN) return false;
        op[n++] = input[idx++];
    }
    op[n] = '\0';
    while (input[idx] == ' ') ++idx;
    n = 0;
    while (input[idx] && input[idx] != ' ') {
        if (n + 1u >= FS_PATH_LEN) return false;
        path[n++] = input[idx++];
    }
    path[n] = '\0';
    while (input[idx] == ' ') ++idx;
    if (string_len(input + idx) >= FS_REQUEST_LEN) return false;
    copy_bounded(data, FS_REQUEST_LEN, input + idx);
    return true;
}
static bool is_fs_op(const char *op)
{
    return equals(op, "ls") || equals(op, "pwd") || equals(op, "mkdir") ||
           equals(op, "touch") || equals(op, "cd") || equals(op, "cat") ||
           equals(op, "write") || equals(op, "append") || equals(op, "rm") ||
           equals(op, "rmdir") || equals(op, "fsinfo") ||
           equals(op, "diskinfo") || equals(op, "sync");
}
static bool start_fs_command(const char *input)
{
    char op[FS_OP_LEN], path[FS_PATH_LEN], data[FS_REQUEST_LEN];
    if (!parse_fs_command(input, op, path, data)) {
        emit("Command too long (try a shorter path or text).\n");
        return false;
    }
    if (!is_fs_op(op)) return false;
    bool takes_content = equals(op, "write") || equals(op, "append");
    bool needs_path = equals(op, "touch") || equals(op, "mkdir") ||
                      equals(op, "cat") || equals(op, "rm") ||
                      equals(op, "rmdir") || takes_content;
    if (needs_path && path[0] == '\0') {
        emit("Usage: "); emit(op); emit(" <path>");
        if (takes_content) emit(" <text>");
        emit("\n");
        return false;
    }
    if (takes_content && data[0] == '\0') {
        emit("Usage: "); emit(op); emit(" <path> <text>\n");
        return false;
    }
    if (!takes_content && data[0] != '\0') {
        emit("Unexpected argument. Paths cannot contain spaces.\n");
        return false;
    }
    fs_mailbox_t *m = (fs_mailbox_t *)fs_mailbox_vaddr;
    copy_bounded(m->operation, FS_OP_LEN, op);
    copy_bounded(m->path, FS_PATH_LEN, path);
    copy_bounded(m->data, FS_REQUEST_LEN, data);
    awaiting_sequence = __atomic_load_n(&m->request_seq, __ATOMIC_RELAXED) + 1u;
    waiting_fs = true;
    __atomic_store_n(&m->request_seq, awaiting_sequence, __ATOMIC_RELEASE);
    microkit_notify(CH_FILESYSTEM);
    return true;
}

/* Submit one application request; manager performs dispatch in its own PD. */
static bool start_app_command(const char *operation, const char *arguments)
{
    if (string_len(arguments) >= APP_ARGS_CAP) {
        emit("Application command too long.\n");
        return false;
    }
    app_mailbox_t *m = (app_mailbox_t *)app_mailbox_vaddr;
    copy_bounded(m->operation, APP_OP_CAP, operation);
    copy_bounded(m->args, APP_ARGS_CAP, arguments);
    awaiting_app_sequence = __atomic_load_n(&m->request_seq, __ATOMIC_RELAXED) + 1u;
    waiting_app = true;
    __atomic_store_n(&m->request_seq, awaiting_app_sequence, __ATOMIC_RELEASE);
    microkit_notify(CH_APPMANAGER);
    return true;
}
/* Returns true if we have an outstanding filesystem request. */
static bool run_command(const char *cmd)
{
    if (equals(cmd, "")) {
        return false;
    } else if (equals(cmd, "help")) {
        emit("Commands:\n");
        emit("  help, about, version, echo <text>, clear, devices, ipc\n");
        emit("  apps                List available isolated applications\n");
        emit("  run calc 7 + 5      Execute isolated calculator PD\n");
        emit("  run counter 8       Execute isolated counter PD\n");
        emit("  ps                  List static PDs and app job counts\n");
        emit("  exec <path>         Interpret .mos text from disk\n");
        emit("  elfrun <path> [n]   Run restricted native AArch64 ELF with arg 0..10000\n");
        emit("  pwd                 Show current directory\n");
        emit("  ls [path]           List files and folders\n");
        emit("  cd [path]           Change directory (default /)\n");
        emit("  mkdir <path>        Create a directory\n");
        emit("  touch <path>        Create an empty file\n");
        emit("  write <path> <text> Create/overwrite a text file\n");
        emit("  append <path> <text> Append text to a file\n");
        emit("  cat <path>          Display a text file\n");
        emit("  rm <path>           Delete a file\n");
        emit("  rmdir <path>        Delete an empty directory\n");
        emit("  fsinfo              Filesystem statistics + unsaved status\n");
        emit("  diskinfo            VirtIO block disk and checkpoint info\n");
        emit("  sync                Save files and directories to disk\n");
        emit("MicroFS paths support /, . and ..; use sync to persist changes.\n");
    } else if (equals(cmd, "about")) {
        emit("Micro-OS: C / seL4 Microkit, nine isolated protection domains.\n");
        emit("Drivers, shell, FS, manager, two C apps and script VM use IPC.\n");
    } else if (equals(cmd, "version")) {
        emit("Micro-OS v0.8 / seL4 + Microkit 2.4.0 / AArch64 QEMU\n");
    } else if (equals(cmd, "apps")) {
        return start_app_command("apps", "");
    } else if (starts_with(cmd, "elfrun ") || starts_with(cmd, "exec ")) {
        bool native = starts_with(cmd, "elfrun ");
        const char *path = cmd + (native ? 7 : 5);
        char fs_path[FS_PATH_LEN];
        unsigned length = 0;
        while (path[length] != '\0' && path[length] != ' ') ++length;
        if (length == 0u || length >= FS_PATH_LEN) {
            emit("Usage: exec <path> | elfrun <path> [0..10000]\n"); return false;
        }
        for (unsigned j=0;j<length;++j) fs_path[j]=path[j];
        fs_path[length]='\0';
        native_input = 0;
        if (path[length] != '\0') {
            if (!native || path[length] != ' ') {
                emit("exec: extra arguments not supported\n"); return false;
            }
            const char *p = path + length + 1u;
            if (*p == '\0') { emit("elfrun: expected numeric argument\n"); return false; }
            unsigned x=0;
            while (*p >= '0' && *p <= '9') {
                x=x*10u+(unsigned)(*p++-'0');
                if (x>10000u) { emit("elfrun: argument must be 0..10000\n"); return false; }
            }
            if (*p != '\0') { emit("elfrun: invalid argument\n"); return false; }
            native_input=x;
        }
        fs_mailbox_t *m = (fs_mailbox_t *)fs_mailbox_vaddr;
        copy_bounded(m->operation, FS_OP_LEN, "load");
        copy_bounded(m->path, FS_PATH_LEN, fs_path);
        m->data[0] = '\0';
        awaiting_sequence = __atomic_load_n(&m->request_seq, __ATOMIC_RELAXED) + 1u;
        waiting_fs = true;
        waiting_exec_fs = !native;
        waiting_native_fs = native;
        __atomic_store_n(&m->request_seq, awaiting_sequence, __ATOMIC_RELEASE);
        microkit_notify(CH_FILESYSTEM);
        return true;
    } else if (equals(cmd, "elfrun")) {
        emit("Usage: elfrun /apps/square.elf 9\n");
    } else if (equals(cmd, "exec")) {
        emit("Usage: exec /apps/demo.mos\n");
    } else if (equals(cmd, "ps")) {
        return start_app_command("ps", "");
    } else if (starts_with(cmd, "run ")) {
        return start_app_command("run", cmd + 4);
    } else if (equals(cmd, "run")) {
        emit("Usage: run calc <a> <op> <b> | run counter <1..30>\n");
    } else if (equals(cmd, "echo")) {
        emit("\n");
    } else if (starts_with(cmd, "echo ")) {
        emit(cmd + 5); emit("\n");
    } else if (equals(cmd, "clear")) {
        emit("\x1b[2J\x1b[H");
    } else if (equals(cmd, "devices")) {
        emit("uart0: ARM PL011 serial device, IRQ 33 (UART driver PD)\n");
        emit("microfs: filesystem service; persistent after sync\n");
        emit("virtio-blk0: VirtIO MMIO block device (block driver PD, IRQ 79)\n");
    } else if (equals(cmd, "ipc")) {
        emit("Terminal <-> UART: separate shared RX/TX rings and notifications.\n");
        emit("Terminal <-> filesystem: shared request/reply mailbox + notifications.\n");
        emit("Filesystem <-> block driver: shared sector mailbox + notifications.\n");
        emit("UART and VirtIO drivers alone map their respective device MMIO.\n");
        emit("Terminal <-> app manager: mailbox + notifications.\n");
        emit("App manager <-> each isolated app: separate mailbox + notifications.\n");
    } else {
        char op[FS_OP_LEN], path[FS_PATH_LEN], data[FS_REQUEST_LEN];
        if (parse_fs_command(cmd, op, path, data) && is_fs_op(op)) {
            return start_fs_command(cmd);
        }
        emit("Unknown command: "); emit(cmd);
        emit("\nType 'help' for available commands.\n");
    }
    return false;
}
static void handle_input(uint8_t ch)
{
    /* A single-command-at-a-time protocol: don't read another command while
     * the filesystem is servicing one. Under heavy typing this loses typeahead.
     */
    if (waiting_fs || waiting_app) return;
    if (ch == '\n' && last_was_cr) { last_was_cr = false; return; }
    last_was_cr = (ch == '\r');
    if (ch == '\r' || ch == '\n') {
        emit("\n");
        line[line_length] = '\0';
        bool pending = run_command(line);
        line_length = 0;
        if (!pending) prompt();
    } else if (ch == 8u || ch == 127u) {
        if (line_length > 0u) { --line_length; emit("\b \b"); }
    } else if (ch >= 32u && ch <= 126u) {
        if (line_length < MAX_LINE - 1u) {
            line[line_length++] = (char)ch;
            emit_char((char)ch);
        }
    }
}
void init(void)
{
    line_length = 0;
    last_was_cr = false;
    waiting_fs = false;
    waiting_app = false;
    waiting_exec_fs = false;
    waiting_native_fs = false;
    awaiting_sequence = 0;
    awaiting_app_sequence = 0;
    emit("\n=============================================\n");
    emit("  MICRO-OS v0.8 | seL4 Microkit 2.4.0\n");
    emit("  Nine PDs: shell + drivers + FS + manager + 4 apps\n");
    emit("=============================================\n");
    emit("Welcome! Try 'elfrun /apps/square.elf 9', 'apps'.\n\n");
    prompt();
    microkit_notify(CH_UART_DRIVER);
}
void notified(microkit_channel channel)
{
    if (channel == CH_UART_DRIVER) {
        ring_t *rx = (ring_t *)rx_ring_vaddr;
        uint8_t byte;
        while (ring_pop(rx, &byte)) handle_input(byte);
        microkit_notify(CH_UART_DRIVER); /* flush echoed input and output */
    } else if (channel == CH_APPMANAGER) {
        app_mailbox_t *m = (app_mailbox_t *)app_mailbox_vaddr;
        uint32_t done = __atomic_load_n(&m->response_seq, __ATOMIC_ACQUIRE);
        if (waiting_app && done == awaiting_app_sequence) {
            waiting_app = false;
            emit(m->reply);
            prompt();
            microkit_notify(CH_UART_DRIVER);
        }
    } else if (channel == CH_FILESYSTEM) {
        fs_mailbox_t *m = (fs_mailbox_t *)fs_mailbox_vaddr;
        uint32_t done = __atomic_load_n(&m->response_seq, __ATOMIC_ACQUIRE);
        if (waiting_fs && done == awaiting_sequence) {
            waiting_fs = false;
            if (waiting_native_fs) {
                waiting_native_fs = false;
                if (starts_with(m->reply, "OK:")) {
                    uint8_t *code=(uint8_t *)native_code_vaddr;
                    native_image_info_t info;
                    if (native_parse_b64_ex(m->reply+3, code, &info)) {
#ifdef __aarch64__
                        /* Clean D-cache lines containing freshly written code.
                         * Future functions might cross a 64-byte line. */
                        uintptr_t start=(native_code_vaddr + info.entry_offset);
                        uintptr_t end=start+(info.takes_argument?NATIVE_CODE_SIZE:8u);
                        for (uintptr_t a=start&~(uintptr_t)63u; a<end; a+=64u)
                            __asm__ volatile("dc cvau, %0" :: "r"(a) : "memory");
                        __asm__ volatile("dsb ish" ::: "memory");
#endif
                        char runargs[32];
                        unsigned pos=0;
                        unsigned entry=info.entry_offset;
                        /* Valid entries are precisely 120 or 128. */
                        runargs[pos++]='1'; runargs[pos++]='2';
                        runargs[pos++]=entry==120u?'0':'8'; runargs[pos++]=' ';
                        char reverse[12]; unsigned k=0, x=native_input;
                        do { reverse[k++]=(char)('0'+(x%10u)); x/=10u; } while(x);
                        while(k) runargs[pos++]=reverse[--k];
                        runargs[pos]='\0';
                        if (start_app_command("elfrun", runargs)) {
                            microkit_notify(CH_UART_DRIVER);
                            return;
                        }
                    } else {
                        emit("elfrun: unsupported ELF or unsafe machine instructions.\n");
                    }
                } else {
                    emit(starts_with(m->reply, "ERR:") ? m->reply+4 : "elfrun: filesystem error\n");
                }
            } else if (waiting_exec_fs) {
                waiting_exec_fs = false;
                if (starts_with(m->reply, "OK:")) {
                    /* Files are data, not native ELF code; the script VM interprets. */
                    if (start_app_command("exec", m->reply + 3)) {
                        microkit_notify(CH_UART_DRIVER);
                        return;
                    }
                } else {
                    emit(starts_with(m->reply, "ERR:") ? m->reply + 4 : "exec: filesystem error\n");
                }
            } else {
                emit(m->reply);
            }
            prompt();
            microkit_notify(CH_UART_DRIVER);
        }
    }
}
