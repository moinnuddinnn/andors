/* Micro-OS v0.8 application manager. Statically provisioned worker PDs.
 * The manager is NOT a dynamic seL4 process creator or ELF loader.
 * Its only power is to ask preconfigured workers to perform bounded jobs.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "app_protocol.h"
#include "app_utils.h"

#define CH_TERMINAL 0
#define CH_CALC 1
#define CH_COUNTER 2
#define CH_SCRIPT 3
#define CH_NATIVE 4

uintptr_t app_mailbox_vaddr = 0;
uintptr_t calc_mailbox_vaddr = 0;
uintptr_t counter_mailbox_vaddr = 0;
uintptr_t script_mailbox_vaddr = 0;
uintptr_t native_mailbox_vaddr = 0;
static uint32_t handled_request = 0;
static uint32_t pending_request = 0;
static uint32_t pending_worker = 0;
static unsigned busy_worker = 0; /* 0 none, 1 calc, 2 counter, 3 script, 4 native */
static uint32_t calc_jobs = 0;
static uint32_t counter_jobs = 0;
static uint32_t script_jobs = 0;
static uint32_t native_jobs = 0;
static char calc_status[20] = "READY";
static char counter_status[20] = "READY";
static char script_status[20] = "READY";
static char native_status[20] = "READY";

static app_mailbox_t *client(void) { return (app_mailbox_t *)app_mailbox_vaddr; }
static app_worker_mailbox_t *worker(unsigned index)
{
    return (app_worker_mailbox_t *)(index == 1 ? calc_mailbox_vaddr :
                                   index == 2 ? counter_mailbox_vaddr :
                                   index == 3 ? script_mailbox_vaddr : native_mailbox_vaddr);
}
static void finish(uint32_t request)
{
    __atomic_store_n(&client()->response_seq, request, __ATOMIC_RELEASE);
    microkit_notify(CH_TERMINAL);
}
static void list_apps(void)
{
    str_copy(client()->reply, APP_REPLY_CAP,
        "Available isolated C applications:\n"
        "  calc       Usage: run calc 7 + 5\n"
        "  counter    Usage: run counter 8\n"
        "  script VM  Usage: exec /apps/demo.mos\n"
        "  native ELF  Usage: elfrun /apps/square.elf 9\n"
        "Each app is a separate seL4 Microkit protection domain.\n");
}
static void list_processes(void)
{
    app_mailbox_t *m = client();
    str_copy(m->reply, APP_REPLY_CAP,
        "PD  NAME            STATUS       JOBS\n"
        " 1  uart_driver     SERVICE\n"
        " 2  block_driver    SERVICE\n"
        " 3  filesystem      SERVICE\n"
        " 4  terminal        SERVICE\n"
        " 5  app_manager     SERVICE\n"
        " 6  calculator      ");
    str_append(m->reply, APP_REPLY_CAP, calc_status);
    str_append(m->reply, APP_REPLY_CAP, "       ");
    append_u64(m->reply, APP_REPLY_CAP, calc_jobs);
    str_append(m->reply, APP_REPLY_CAP, "\n 7  counter         ");
    str_append(m->reply, APP_REPLY_CAP, counter_status);
    str_append(m->reply, APP_REPLY_CAP, "       ");
    append_u64(m->reply, APP_REPLY_CAP, counter_jobs);
    str_append(m->reply, APP_REPLY_CAP, "\n 8  script_vm       ");
    str_append(m->reply, APP_REPLY_CAP, script_status);
    str_append(m->reply, APP_REPLY_CAP, "       ");
    append_u64(m->reply, APP_REPLY_CAP, script_jobs);
    str_append(m->reply, APP_REPLY_CAP, "\n 9  native_runner   ");
    str_append(m->reply, APP_REPLY_CAP, native_status);
    str_append(m->reply, APP_REPLY_CAP, "       ");
    append_u64(m->reply, APP_REPLY_CAP, native_jobs);
    str_append(m->reply, APP_REPLY_CAP,
        "\nAll PDs are preconfigured; IDs are display-only.\n");
}
void init(void)
{
    handled_request = pending_request = pending_worker = 0;
    busy_worker = 0;
    calc_jobs = counter_jobs = script_jobs = native_jobs = 0;
    str_copy(calc_status, sizeof(calc_status), "READY");
    str_copy(counter_status, sizeof(counter_status), "READY");
    str_copy(script_status, sizeof(script_status), "READY");
    str_copy(native_status, sizeof(native_status), "READY");
}
void notified(microkit_channel ch)
{
    if (ch == CH_TERMINAL) {
        app_mailbox_t *m = client();
        uint32_t seq = __atomic_load_n(&m->request_seq, __ATOMIC_ACQUIRE);
        if (seq == handled_request) return;
        /* Only one request is outstanding from the terminal. */
        if (busy_worker) return;
        handled_request = seq;
        m->reply[0] = '\0';
        if (str_eq(m->operation, "apps")) {
            list_apps(); finish(seq); return;
        }
        if (str_eq(m->operation, "ps")) {
            list_processes(); finish(seq); return;
        }
        bool is_script = str_eq(m->operation, "exec");
        bool is_native = str_eq(m->operation, "elfrun");
        if (!str_eq(m->operation, "run") && !is_script && !is_native) {
            str_copy(m->reply, APP_REPLY_CAP, "Unknown application operation.\n");
            finish(seq); return;
        }
        const char *args = m->args;
        unsigned which = is_native ? 4u : (is_script ? 3u : 0u);
        if (!is_script && str_prefix(args, "calculator ")) { which = 1; args += 11; }
        else if (!is_script && str_eq(args, "calculator")) { which = 1; args += 10; }
        else if (!is_script && str_prefix(args, "calc ")) { which = 1; args += 5; }
        else if (!is_script && str_eq(args, "calc")) { which = 1; args += 4; }
        else if (!is_script && str_prefix(args, "counter ")) { which = 2; args += 8; }
        else if (!is_script && str_eq(args, "counter")) { which = 2; args += 7; }
        if (!which) {
            str_copy(m->reply, APP_REPLY_CAP, "Unknown app. Use 'apps' to list available programs.\n");
            finish(seq); return;
        }
        app_worker_mailbox_t *w = worker(which);
        str_copy(w->args, APP_ARGS_CAP, args);
        pending_worker = __atomic_load_n(&w->request_seq, __ATOMIC_RELAXED) + 1u;
        pending_request = seq;
        busy_worker = which;
        if (which == 1) str_copy(calc_status, sizeof(calc_status), "RUNNING");
        else if (which == 2) str_copy(counter_status, sizeof(counter_status), "RUNNING");
        else if (which == 3) str_copy(script_status, sizeof(script_status), "RUNNING");
        else str_copy(native_status, sizeof(native_status), "RUNNING");
        __atomic_store_n(&w->request_seq, pending_worker, __ATOMIC_RELEASE);
        microkit_notify(which == 1 ? CH_CALC : which == 2 ? CH_COUNTER : which == 3 ? CH_SCRIPT : CH_NATIVE);
    } else if (ch == CH_CALC || ch == CH_COUNTER || ch == CH_SCRIPT || ch == CH_NATIVE) {
        unsigned which = ch == CH_CALC ? 1u : ch == CH_COUNTER ? 2u : ch == CH_SCRIPT ? 3u : 4u;
        if (busy_worker != which) return;
        app_worker_mailbox_t *w = worker(which);
        uint32_t done = __atomic_load_n(&w->response_seq, __ATOMIC_ACQUIRE);
        if (done != pending_worker) return;
        str_copy(client()->reply, APP_REPLY_CAP, w->reply);
        if (which == 1) { ++calc_jobs; str_copy(calc_status, sizeof(calc_status), "DONE"); }
        else if (which == 2) { ++counter_jobs; str_copy(counter_status, sizeof(counter_status), "DONE"); }
        else if (which == 3) { ++script_jobs; str_copy(script_status, sizeof(script_status), "DONE"); }
        else { ++native_jobs; str_copy(native_status, sizeof(native_status), "DONE"); }
        busy_worker = 0;
        finish(pending_request);
    }
}
