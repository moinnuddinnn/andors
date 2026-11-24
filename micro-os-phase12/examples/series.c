#include <stdint.h>
/* Native loops, arithmetic and indexed loads/stores. No global writable state. */
uint32_t program(uint32_t n,uint32_t *scratch){
    if(n>1000u)n=1000u;
    volatile uint32_t *p=scratch;
    for(uint32_t i=1;i<=n;i++)p[i]=i*i;
    uint32_t sum=0;
    for(uint32_t i=1;i<=n;i++)sum+=p[i];
    p[0]=sum;
    return sum;
}
