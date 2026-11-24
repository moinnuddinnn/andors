#ifndef MICRO_OS_CONSOLE_PROTOCOL_H
#define MICRO_OS_CONSOLE_PROTOCOL_H
#include <stdint.h>
/* Single-writer/single-reader, one bounded fire-and-forget service request.
 * Native worker publishes a number then advances request_seq (release).
 * Console service consumes using acquire and rejects duplicate notifications.
 * One request maximum per native invocation in v1.0.
 */
typedef struct {
    uint32_t request_seq;
    uint32_t number;
} console_mailbox_t;
_Static_assert(sizeof(console_mailbox_t) <= 4096, "console mailbox fits page");
#endif
