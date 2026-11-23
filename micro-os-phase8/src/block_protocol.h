#ifndef MICRO_OS_BLOCK_PROTOCOL_H
#define MICRO_OS_BLOCK_PROTOCOL_H
#include <stdint.h>
#define BLOCK_SIZE 512u
#define BLOCK_READ 1u
#define BLOCK_WRITE 2u
#define BLOCK_OK 0u
#define BLOCK_ERR_NO_DEVICE 1u
#define BLOCK_ERR_IO 2u
#define BLOCK_ERR_RANGE 3u
#define BLOCK_ERR_BUSY 4u
/* Exactly one outstanding request, FS owns requests, driver owns responses.
 * 'data' is copied to/from DMA by the driver; this page is never passed as DMA.
 */
typedef struct {
    uint32_t request_seq;
    uint32_t response_seq;
    uint32_t operation;
    uint32_t status;
    uint64_t sector;
    uint64_t capacity; /* driver writes after probing the device */
    uint8_t data[BLOCK_SIZE];
} block_mailbox_t;
_Static_assert(sizeof(block_mailbox_t) <= 4096, "block mailbox must fit one page");
#endif
