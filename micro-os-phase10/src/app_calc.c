/* Independent calculator process/protection-domain. It has NO UART, disk,
 * shell, or other application's address-space mapping. */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "app_protocol.h"
#include "app_utils.h"

uintptr_t calc_mailbox_vaddr = 0; /* patched by Microkit */
static uint32_t handled = 0;
void init(void) { handled = 0; }
void notified(microkit_channel ch)
{
    if (ch != 0) return;
    app_worker_mailbox_t *m = (app_worker_mailbox_t *)calc_mailbox_vaddr;
    uint32_t seq = __atomic_load_n(&m->request_seq, __ATOMIC_ACQUIRE);
    if (seq == handled) return;
    handled = seq;
    const char *p = m->args;
    int64_t a = 0, b = 0, result = 0;
    char op = '\0';
    bool good = parse_int(&p, &a);
    if (good) {
        skip_spaces(&p);
        op = *p;
        if (op == '+' || op == '-' || op == '*' || op == '/') ++p;
        else good = false;
    }
    if (good) good = parse_int(&p, &b);
    if (good) { skip_spaces(&p); good = (*p == '\0'); }
    m->reply[0] = '\0';
    if (!good) {
        str_copy(m->reply, APP_REPLY_CAP, "Usage: run calc <integer> <+|-|*|/> <integer>\nNumbers: -1000000..1000000\n");
    } else if (op == '/' && b == 0) {
        str_copy(m->reply, APP_REPLY_CAP, "Calculator: division by zero is undefined.\n");
    } else {
        if (op == '+') result = a + b;
        if (op == '-') result = a - b;
        if (op == '*') result = a * b;
        if (op == '/') result = a / b; /* integer quotient */
        str_copy(m->reply, APP_REPLY_CAP, "calculator: ");
        append_i64(m->reply, APP_REPLY_CAP, a);
        char opstr[4] = {' ', op, ' ', '\0'};
        str_append(m->reply, APP_REPLY_CAP, opstr);
        append_i64(m->reply, APP_REPLY_CAP, b);
        str_append(m->reply, APP_REPLY_CAP, " = ");
        append_i64(m->reply, APP_REPLY_CAP, result);
        str_append(m->reply, APP_REPLY_CAP, "\n");
    }
    __atomic_store_n(&m->response_seq, seq, __ATOMIC_RELEASE);
    microkit_notify(0);
}
