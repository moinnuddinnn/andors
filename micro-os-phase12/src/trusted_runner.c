/* Separate preallocated domain for trusted, freestanding native C functions.
 * No filesystem/device capabilities. The worker is NOT a security sandbox for
 * hostile machine code. A child fault is handled by app_manager. */
#include <microkit.h>
#include "app_protocol.h"
#include "app_utils.h"
#include "trusted_elf.h"
uintptr_t trusted_mailbox_vaddr=0;
uintptr_t trusted_exec_vaddr=0;
static uint32_t handled;
static uint32_t scratch[1024] __attribute__((aligned(4096)));
void init(void){handled=0;microkit_dbg_puts("[TRUSTED] native C worker ready\n");}
void notified(microkit_channel ch){
    if(ch!=0)return;
    app_worker_mailbox_t *m=(app_worker_mailbox_t*)trusted_mailbox_vaddr;
    uint32_t seq=__atomic_load_n(&m->request_seq,__ATOMIC_ACQUIRE);
    if(seq==handled)return;
    handled=seq;
    const char *p=m->args;
    int64_t n,x;
    if(!parse_int(&p,&n)||!parse_int(&p,&x)||*p||n<132||n>TRUSTED_ELF_MAX||x<0||x>10000 || !trusted_elf_valid((const uint8_t*)trusted_exec_vaddr,(unsigned)n)){
        str_copy(m->reply,APP_REPLY_CAP,"trusted: invalid image or argument\n");
    }else{
        for(unsigned i=0;i<1024;i++)scratch[i]=0;
#ifdef __aarch64__
        uintptr_t start=trusted_exec_vaddr;
        for(uintptr_t a=start;a<start+(uintptr_t)n;a+=64u) __asm__ volatile("ic ivau, %0"::"r"(a):"memory");
        __asm__ volatile("dsb ish\n isb":::"memory");
        uint32_t (*fn)(uint32_t,uint32_t*)=(uint32_t(*)(uint32_t,uint32_t*))(start+TRUSTED_ENTRY);
        uint32_t result=fn((uint32_t)x,scratch);
        str_copy(m->reply,APP_REPLY_CAP,"trusted C returned ");
        append_u64(m->reply,APP_REPLY_CAP,result);
        str_append(m->reply,APP_REPLY_CAP,"; scratch[0]=");
        append_u64(m->reply,APP_REPLY_CAP,scratch[0]);
        str_append(m->reply,APP_REPLY_CAP,"\n");
#else
        str_copy(m->reply,APP_REPLY_CAP,"Trusted execution requires AArch64\n");
#endif
    }
    __atomic_store_n(&m->response_seq,seq,__ATOMIC_RELEASE);
    microkit_notify(0);
}
