#ifndef MICRO_OS_ELF_LOADER_H
#define MICRO_OS_ELF_LOADER_H
#include <stdint.h>
#include <stdbool.h>
/* v0.9: strict allowlist of three ELF64/AArch64 forms.
 * v0.7: return constant; v0.8: square-and-add; v0.9: square-and-add
 * plus a store to the runner-only private data page through x1.
 * No arbitrary ELF programs, relocations, syscalls or branches. */
#define NATIVE_ELF_SIZE 184u /* maximum accepted ELF image */
#define NATIVE_ENTRY 120u    /* v0.7 compatibility */
#define NATIVE_C_ENTRY 128u /* v0.8 C function */
#define NATIVE_DATA_ENTRY 144u /* v0.9 isolated data writer */
#define NATIVE_CODE_SIZE 16u
#define NATIVE_ABI_ENTRY 136u /* v1.0 printed-number callback */
#define NATIVE_ABI_CODE_SIZE 40u
#define NATIVE_PAGE_SIZE 4096u
typedef struct {
    uint32_t entry_offset;
    uint32_t file_size;
    uint32_t constant;
    bool takes_argument;
    bool writes_data;
    bool uses_console;
} native_image_info_t;
bool native_parse_b64_ex(const char *b64, uint8_t dest[NATIVE_ELF_SIZE], native_image_info_t *info);
/* Compatibility API used by the original v0.7 tests. */
bool native_parse_b64(const char *b64, uint8_t dest[NATIVE_ELF_SIZE], uint32_t *constant);
#endif
