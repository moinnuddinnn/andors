#include <stdint.h>
/* Deliberate controlled fault in a disposable guest worker, not hardware. */
uint32_t program(uint32_t x,uint32_t *scratch){
    (void)scratch;
    __asm__ volatile("udf #0");
    return x;
}
