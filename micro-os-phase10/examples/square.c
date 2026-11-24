/* Micro-OS v0.8 restricted C function ABI.
 * The value arrives in AArch64 register w0 and the result returns in w0.
 * The first loader accepts only the code shape x*x+K, with 0<=K<=65535.
 */
#include <stdint.h>
uint32_t program(uint32_t x)
{
    return x * x + 7u;
}
