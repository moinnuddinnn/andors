#ifndef MICRO_OS_APP_PROTOCOL_H
#define MICRO_OS_APP_PROTOCOL_H
#include <stdint.h>

/* Single-flight shell <-> manager request mailbox. Both sides use sequence
 * counters and acquire/release atomics. The notification carries no payload. */
#define APP_OP_CAP 16u
#define APP_ARGS_CAP 192u
#define APP_REPLY_CAP 1024u
typedef struct {
    uint32_t request_seq;   /* terminal writes */
    uint32_t response_seq;  /* manager writes */
    char operation[APP_OP_CAP];
    char args[APP_ARGS_CAP];
    char reply[APP_REPLY_CAP];
} app_mailbox_t;

/* Exactly one outstanding request per worker; manager owns request_seq,
 * worker owns response_seq. Separate regions prevent cross-app interference. */
typedef struct {
    uint32_t request_seq;
    uint32_t response_seq;
    char args[APP_ARGS_CAP];
    char reply[APP_REPLY_CAP];
} app_worker_mailbox_t;
_Static_assert(sizeof(app_mailbox_t) <= 4096u, "app mailbox must fit page");
_Static_assert(sizeof(app_worker_mailbox_t) <= 4096u, "worker mailbox must fit page");
#endif
