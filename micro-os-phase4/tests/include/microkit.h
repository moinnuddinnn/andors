#ifndef MICROKIT_TEST_STUB_H
#define MICROKIT_TEST_STUB_H
#include <stdint.h>
typedef int microkit_channel;
void microkit_notify(microkit_channel ch);
void microkit_dbg_puts(const char *message);
void microkit_irq_ack(microkit_channel ch);
#endif
