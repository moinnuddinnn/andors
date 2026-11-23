/* v0.9: three allowlisted native function patterns in one precreated RX PD.
 * Executable region is RX here; RW only in terminal; never grant RWX.
 * This is NOT a general code sandbox or a dynamic process loader.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "app_protocol.h"
#include "app_utils.h"
#include "elf_loader.h"
uintptr_t native_mailbox_vaddr = 0;
uintptr_t native_exec_vaddr = 0;
/* RW data is mapped only into this runner PD, never into the terminal. */
uintptr_t native_data_vaddr = 0;
static uint32_t handled;
void init(void) { handled=0; microkit_dbg_puts("[NATIVE] AArch64 runner ready (restricted native ABI)\n"); }
static bool decimal(const char **pp, unsigned *result) {
    const char *p=*pp;
    unsigned v=0, digits=0;
    while(*p>='0' && *p<='9' && digits<6u) {
        v=v*10u+(unsigned)(*p++-'0'); ++digits;
    }
    if(!digits || (*p>='0' && *p<='9')) return false;
    *pp=p; *result=v; return true;
}
void notified(microkit_channel ch) {
    if(ch!=0) return;
    app_worker_mailbox_t *m=(app_worker_mailbox_t *)native_mailbox_vaddr;
    uint32_t seq=__atomic_load_n(&m->request_seq,__ATOMIC_ACQUIRE);
    if(seq==handled) return;
    handled=seq;
    const char *p=m->args;
    unsigned entry=0, x=0;
    bool valid=decimal(&p,&entry) && *p++==' ' && decimal(&p,&x) && *p=='\0' &&
               (entry==NATIVE_ENTRY || entry==NATIVE_C_ENTRY || entry==NATIVE_DATA_ENTRY) &&
               x<=10000u && (entry!=NATIVE_DATA_ENTRY || native_data_vaddr!=0);
    if(!valid) {
        str_copy(m->reply,APP_REPLY_CAP,"native: invalid entry or argument\n");
    } else {
#ifdef __aarch64__
        /* Invalidate every I-cache line that contains the program. The v0.8
         * code can cross a line boundary: start at 128, 12-byte code. */
        uintptr_t start=native_exec_vaddr+entry;
        uintptr_t end=start+(entry==NATIVE_ENTRY?8u:
                            entry==NATIVE_C_ENTRY?12u:NATIVE_CODE_SIZE);
        for(uintptr_t line=start&~(uintptr_t)63u;line<end;line+=64u)
            __asm__ volatile("ic ivau, %0" :: "r"(line) : "memory");
        __asm__ volatile("dsb ish\n isb" ::: "memory");
        if (entry==NATIVE_DATA_ENTRY) {
            /* Reset observable output before calling native C code. */
            volatile uint32_t *data=(volatile uint32_t *)native_data_vaddr;
            *data=0xdeadbeefu;
            uint32_t (*fn)(uint32_t, uint32_t *)=
                (uint32_t(*)(uint32_t,uint32_t*))start;
            uint32_t result=fn(x,(uint32_t*)native_data_vaddr);
            str_copy(m->reply, APP_REPLY_CAP, "native C wrote ");
            append_u64(m->reply, APP_REPLY_CAP, *data);
            str_append(m->reply, APP_REPLY_CAP, " to private memory; returned ");
            append_u64(m->reply, APP_REPLY_CAP, result);
            str_append(m->reply, APP_REPLY_CAP, "\n");
        } else {
            uint32_t (*fn)(uint32_t)=(uint32_t(*)(uint32_t))start;
            uint32_t result=fn(x);
            str_copy(m->reply, APP_REPLY_CAP, "native ELF returned ");
            append_u64(m->reply, APP_REPLY_CAP, result);
            str_append(m->reply, APP_REPLY_CAP, " (AArch64 machine code)\n");
        }
#else
        str_copy(m->reply, APP_REPLY_CAP, "Native execution requires AArch64\n");
#endif
    }
    __atomic_store_n(&m->response_seq,seq,__ATOMIC_RELEASE);
    microkit_notify(0);
}
