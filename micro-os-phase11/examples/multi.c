/* v1.1: a single native C invocation requests two independent console
 * formatting services, through a restricted OS callback provided in x1. */
#include <stdint.h>
typedef void (*micro_os_service_fn)(uint32_t operation, uint32_t value);
uint32_t program(uint32_t x, micro_os_service_fn service)
{
    service(1u, x); /* decimal */
    service(2u, x); /* hexadecimal */
    return x;
}
