#include <stdint.h>
/* v0.9: a native C program that WRITES to its runner-private data page.
 * The ABI passes input in w0 and private output address in x1.
 * This is not an OS syscall; it is a narrow, controlled function ABI. */
uint32_t program(uint32_t x, uint32_t *output)
{
    uint32_t value = x * x + 7u;
    output[0] = value;
    return value;
}
