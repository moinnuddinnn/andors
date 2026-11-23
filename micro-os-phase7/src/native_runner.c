/* Native ELF demonstration. Execution occurs in a separate, precreated PD.
 * Executable code region is RX here; writable ONLY in terminal PD.
 * All code currently must be movz w0, #imm16; ret, validated by loader.
 */
#include <stdint.h>
#include <microkit.h>
#include "app_protocol.h"
#include "app_utils.h"
#include "elf_loader.h"
uintptr_t native_mailbox_vaddr = 0;
uintptr_t native_exec_vaddr = 0;
static uint32_t handled;
void init(void) { handled=0; microkit_dbg_puts("[NATIVE] AArch64 runner ready (preallocated PD)\n"); }
void notified(microkit_channel ch) {
    if (ch!=0) return;
    app_worker_mailbox_t *m=(app_worker_mailbox_t *)native_mailbox_vaddr;
    uint32_t seq=__atomic_load_n(&m->request_seq,__ATOMIC_ACQUIRE);
    if (seq==handled) return;
    handled=seq;
    /* This is a fixed, verified 2-instruction function. No syscall ABI.
     * Cache maintenance is required when executing freshly written code.
     * QEMU Cortex-A53 uses 64-byte I cache lines. Kernel needs to allow
     * EL0 user cache operations (standard seL4 AArch64 setting). */
#ifdef __aarch64__
    uintptr_t addr=native_exec_vaddr+NATIVE_ENTRY;
    __asm__ volatile ("ic ivau, %0" :: "r" (addr&~(uintptr_t)63) : "memory");
    __asm__ volatile ("dsb ish\n isb" ::: "memory");
    uint32_t (*entry)(void)=(uint32_t(*)(void))addr;
    uint32_t result=entry();
    str_copy(m->reply, APP_REPLY_CAP, "native ELF returned ");
    append_u64(m->reply, APP_REPLY_CAP, result);
    str_append(m->reply, APP_REPLY_CAP, " (AArch64 machine code)\n");
#else
    str_copy(m->reply, APP_REPLY_CAP, "Native execution requires AArch64\n");
#endif
    __atomic_store_n(&m->response_seq,seq,__ATOMIC_RELEASE);
    microkit_notify(0);
}
