#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_loader.h"
static const char *sample =
"f0VMRgIBAQAAAAAAAAAAAAMAtwABAAAAeAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAEAAOAABAAAAAAAAAAEAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAACAAAAAAAAAABAAAAAAAAAAQAWAUsADX9Y=";
int main(void) {
    uint8_t bin[NATIVE_ELF_SIZE]; uint32_t value=0;
    if(!native_parse_b64(sample,bin,&value) || value != 42) {
        fputs("FAIL: valid ELF rejected\n",stderr); return 1;
    }
    char corrupted[180]; strcpy(corrupted,sample);
    corrupted[0]='!';
    if (native_parse_b64(corrupted,bin,&value)) { fputs("FAIL: bad base64\n",stderr); return 1; }
    strcpy(corrupted,sample); corrupted[172-1]='X';
    if(native_parse_b64(corrupted,bin,&value)){ fputs("FAIL: no padding\n",stderr); return 1; }
    strcpy(corrupted,sample); corrupted[0]='A';
    if(native_parse_b64(corrupted,bin,&value)){ fputs("FAIL: invalid ELF header\n",stderr); return 1; }

    /* v0.8 generated sample comes from real Clang-compiled C .text. */
    const char *compiled = "f0VMRgIBAQAAAAAAAAAAAAMAtwABAAAAgAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAEAAOAABAAAAAAAAAAEAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAoAAAAAAAAACgAAAAAAAAABAAAAAAAAAAAAAAAAAAAADoAIBSACAAG8ADX9YAAAAAAAAAAAAAAAAAAAAAAAAAAA==";
    native_image_info_t info;
    if (!native_parse_b64_ex(compiled, bin, &info) || !info.takes_argument ||
        info.entry_offset!=128u || info.constant!=7u) {
        fputs("FAIL: compiled C function rejected\n",stderr); return 1;
    }
    if (bin[128]!=0xe8 || bin[129]!=0 || bin[136]!=0xc0) {
        fputs("FAIL: wrong compiled machine code\n",stderr); return 1;
    }
    /* A single-bit corruption in the ADD/MADD instruction must fail. */
    char mutated[240]; strcpy(mutated,compiled);
    mutated[181] = mutated[181]=='A'?'B':'A';
    if (native_parse_b64_ex(mutated,bin,&info)) {
        fputs("FAIL: corrupted C ELF accepted\n",stderr); return 1;
    }
    puts("PASS: strict Base64 ELF64/AArch64 legacy return and C function validation");
    return 0;
}
