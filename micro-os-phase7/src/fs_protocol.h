#ifndef MICRO_OS_FS_PROTOCOL_H
#define MICRO_OS_FS_PROTOCOL_H

#include <stdint.h>

/* One outstanding request at a time; all fields fit a single shared 4-KiB page.
 * The terminal writes the command fields and request_seq. The filesystem writes
 * the reply and response_seq. Notifications signal *availability*, not payloads.
 */
#define FS_OP_LEN       16u
#define FS_PATH_LEN     96u
#define FS_REQUEST_LEN 192u
#define FS_REPLY_LEN   1024u

typedef struct {
    uint32_t request_seq; /* written only by the terminal */
    uint32_t response_seq; /* written only by the filesystem */
    char operation[FS_OP_LEN];
    char path[FS_PATH_LEN];
    char data[FS_REQUEST_LEN];
    char reply[FS_REPLY_LEN];
} fs_mailbox_t;

_Static_assert(sizeof(fs_mailbox_t) <= 4096u, "FS mailbox must fit one page");

#endif
