/* First user-space component of our seL4 Microkit-based OS.
 * Output uses the Microkit debug interface, not a UART driver yet.
 */
#include <microkit.h>

void init(void)
{
    microkit_dbg_puts(
        "Hello from our microkernel OS!\n"
    );
}

void notified(microkit_channel channel)
{
    (void)channel;
}
