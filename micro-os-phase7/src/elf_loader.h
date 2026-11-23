#ifndef MICRO_OS_ELF_LOADER_H
#define MICRO_OS_ELF_LOADER_H
#include <stdint.h>
#include <stdbool.h>
/* Phase 7: intentionally tiny ELF64/AArch64 loader for a two-instruction
 * ET_DYN program. No relocations, dynamic linking, syscalls, TLS or BSS.
 * The file's 128 raw bytes are encoded as base64 in MicroFS (172 chars).
 */
#define NATIVE_ELF_SIZE 128u
#define NATIVE_ENTRY 120u
#define NATIVE_PAGE_SIZE 4096u
bool native_parse_b64(const char *b64, uint8_t dest[NATIVE_ELF_SIZE], uint32_t *return_value);
#endif
