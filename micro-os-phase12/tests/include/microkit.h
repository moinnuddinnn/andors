#ifndef MICROKIT_TEST_STUB_H
#define MICROKIT_TEST_STUB_H
#include <stdint.h>
typedef int microkit_channel;
typedef int microkit_child;
typedef uint64_t microkit_msginfo;
typedef int seL4_Bool;
#define seL4_False 0
static inline uint64_t microkit_msginfo_get_label(microkit_msginfo m){return m;}
void microkit_notify(microkit_channel ch);
void microkit_dbg_puts(const char *message);
void microkit_irq_ack(microkit_channel ch);
#endif
