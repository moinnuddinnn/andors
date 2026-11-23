/* Second independent application PD. No MMIO and no shared memory with calc. */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "app_protocol.h"
#include "app_utils.h"
uintptr_t counter_mailbox_vaddr = 0;
static uint32_t handled = 0;
void init(void) { handled = 0; }
void notified(microkit_channel ch)
{
    if (ch != 0) return;
    app_worker_mailbox_t *m = (app_worker_mailbox_t *)counter_mailbox_vaddr;
    uint32_t seq = __atomic_load_n(&m->request_seq, __ATOMIC_ACQUIRE);
    if (seq == handled) return;
    handled = seq;
    const char *p = m->args;
    int64_t n = 0;
    bool valid = parse_int(&p, &n);
    skip_spaces(&p);
    if (*p || n < 1 || n > 30) valid = false;
    m->reply[0] = '\0';
    if (!valid) str_copy(m->reply, APP_REPLY_CAP, "Usage: run counter <1..30>\n");
    else {
        str_copy(m->reply, APP_REPLY_CAP, "counter: ");
        for (int64_t x = 1; x <= n; x++) {
            append_i64(m->reply, APP_REPLY_CAP, x);
            if (x < n) str_append(m->reply, APP_REPLY_CAP, " ");
        }
        str_append(m->reply, APP_REPLY_CAP, "\n");
    }
    __atomic_store_n(&m->response_seq, seq, __ATOMIC_RELEASE);
    microkit_notify(0);
}
