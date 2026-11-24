#ifndef MOS_TRUSTED_ELF_H
#define MOS_TRUSTED_ELF_H
#include <stdbool.h>
#include <stdint.h>
#define TRUSTED_ELF_MAX 2048u
#define TRUSTED_CHUNK 180u
#define TRUSTED_ENTRY 128u
/* Trusted programs only: structure checks are not a machine-code sandbox. */
bool trusted_elf_valid(const uint8_t *image, unsigned length);
bool trusted_chunk_decode(const char *encoded, uint8_t *out, unsigned bytes);
uint32_t trusted_crc32(const uint8_t *data, unsigned length);
#endif
