/* v1.0 exact allowlisted native C function. A user-space ABI callback is
 * supplied by the preallocated native-runner. That callback forwards one
 * integer to the independent console-service PD using Microkit IPC.
 * Not a general-purpose syscall, fork/exec, or arbitrary ELF execution.
 */
#include <stdint.h>
typedef void (*micro_os_log_fn)(uint32_t);
uint32_t program(uint32_t x, micro_os_log_fn log_number) {
    log_number(x);
    return x * x + 7u;
}
