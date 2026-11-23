/* Micro-OS v0.6: bounded text-program interpreter in its own protection domain.
 * Scripts come from MicroFS via terminal and manager IPC. This is NOT native
 * ELF loading, native code execution, or dynamic seL4 process creation.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "app_protocol.h"
#include "app_utils.h"

uintptr_t script_mailbox_vaddr = 0;
static uint32_t last_request = 0;
static app_worker_mailbox_t *mb(void) { return (app_worker_mailbox_t *)script_mailbox_vaddr; }

static void output(const char *s) { str_append(mb()->reply, APP_REPLY_CAP, s); }
static void number(int64_t x) { append_i64(mb()->reply, APP_REPLY_CAP, x); }

static bool only_spaces(const char *p)
{
    while (*p == ' ') ++p;
    return *p == '\0';
}
static const char *skip(const char *p)
{
    while (*p == ' ') ++p;
    return p;
}
static bool execute_statement(const char *stmt)
{
    const char *p = skip(stmt);
    if (*p == '\0') return true;
    if (str_prefix(p, "print ")) {
        output(p + 6);
        output("\n");
        return true;
    }
    if (str_eq(p, "print")) { output("\n"); return true; }
    if (str_prefix(p, "add ")) {
        p += 4;
        int64_t a, b;
        if (!parse_int(&p, &a) || !parse_int(&p, &b) || !only_spaces(p)) {
            output("script: add expects two integers (-1000000..1000000)\n");
            return false;
        }
        number(a); output(" + "); number(b); output(" = "); number(a+b);
        output("\n");
        return true;
    }
    if (str_prefix(p, "count ")) {
        p += 6;
        int64_t n;
        if (!parse_int(&p, &n) || !only_spaces(p) || n < 1 || n > 20) {
            output("script: count expects 1..20\n");
            return false;
        }
        output("count:");
        for (int64_t i=1; i<=n; ++i) { output(" "); number(i); }
        output("\n");
        return true;
    }
    output("script: unknown statement: "); output(p); output("\n");
    return false;
}
static void execute(const char *program)
{
    app_worker_mailbox_t *m=mb();
    m->reply[0]='\0';
    size_t len = str_length(program);
    if (len == 0u || len > 255u) {
        output("script: program must be 1..255 bytes\n");
        return;
    }
    unsigned count=0;
    unsigned offset=0;
    while (offset<len) {
        if (++count>12u) { output("script: instruction limit (12) exceeded\n"); break; }
        char statement[256];
        unsigned n=0;
        while (offset<len && program[offset]!=';') statement[n++]=program[offset++];
        if (offset<len && program[offset]==';') ++offset;
        while (n>0u && statement[n-1u]==' ') --n;
        statement[n]='\0';
        if (str_eq(skip(statement), "end")) break;
        if (!execute_statement(statement)) break;
    }
}
void init(void)
{
    last_request=0;
}
void notified(microkit_channel channel)
{
    if (channel!=0) return;
    app_worker_mailbox_t *m=mb();
    uint32_t seq=__atomic_load_n(&m->request_seq,__ATOMIC_ACQUIRE);
    if (seq==last_request) return;
    last_request=seq;
    execute(m->args);
    __atomic_store_n(&m->response_seq,seq,__ATOMIC_RELEASE);
    microkit_notify(0);
}
