#ifndef MICRO_OS_ELF_LOADER_H
#define MICRO_OS_ELF_LOADER_H
#include <stdint.h>
#include <stdbool.h>
/* v0.8: accept existing v0.7 legacy ELF (128 B) and a new 160-byte ELF
 * whose 12-byte function was compiled from freestanding C. Both are strict,
 * allowlisted functions (no branches, memory accesses, syscalls or relocations).
 * Base64 text is stored in MicroFS, not raw binary.
 */
#define NATIVE_ELF_SIZE 160u /* maximum accepted ELF image */
#define NATIVE_ENTRY 120u    /* v0.7 compatibility */
#define NATIVE_C_ENTRY 128u /* v0.8 C function */
#define NATIVE_CODE_SIZE 12u
#define NATIVE_PAGE_SIZE 4096u
typedef struct {
    uint32_t entry_offset;
    uint32_t file_size;
    uint32_t constant;
    bool takes_argument;
} native_image_info_t;
bool native_parse_b64_ex(const char *b64, uint8_t dest[NATIVE_ELF_SIZE], native_image_info_t *info);
/* Compatibility API used by the original v0.7 tests. */
bool native_parse_b64(const char *b64, uint8_t dest[NATIVE_ELF_SIZE], uint32_t *constant);
#endif
