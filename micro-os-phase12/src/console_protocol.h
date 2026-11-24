#ifndef MICRO_OS_CONSOLE_PROTOCOL_H
#define MICRO_OS_CONSOLE_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
/* v1.1: one producer (native runner), one consumer (console service).
 * Notifications are wakeups only; the ring carries the data. The consumer
 * writes read_seq to acknowledge consumed entries, preventing overwrite.
 */
#define CONSOLE_QUEUE_CAP 8u
#define CONSOLE_OP_PRINT_DEC 1u
#define CONSOLE_OP_PRINT_HEX 2u
typedef struct {
    uint32_t op;
    uint32_t value;
} console_request_t;
typedef struct {
    uint32_t write_seq; /* producer only */
    uint32_t read_seq;  /* consumer only */
    console_request_t requests[CONSOLE_QUEUE_CAP];
} console_mailbox_t;
_Static_assert(sizeof(console_mailbox_t) <= 4096u, "console queue fits page");
static inline bool console_enqueue(console_mailbox_t *queue, uint32_t op, uint32_t value)
{
    if (op != CONSOLE_OP_PRINT_DEC && op != CONSOLE_OP_PRINT_HEX) return false;
    uint32_t write = __atomic_load_n(&queue->write_seq, __ATOMIC_RELAXED);
    uint32_t read = __atomic_load_n(&queue->read_seq, __ATOMIC_ACQUIRE);
    if ((uint32_t)(write - read) >= CONSOLE_QUEUE_CAP) return false;
    queue->requests[write % CONSOLE_QUEUE_CAP] = (console_request_t){ op, value };
    __atomic_store_n(&queue->write_seq, write + 1u, __ATOMIC_RELEASE);
    return true;
}
#endif
