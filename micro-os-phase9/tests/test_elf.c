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
    /* v0.9 Clang-generated output includes a single validated memory store. */
    const char *data_elf = "f0VMRgIBAQAAAAAAAAAAAAMAtwABAAAAkAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAEAAOAABAAAAAAAAAAEAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAsAAAAAAAAACwAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA6ACAUgAgABsgAAC5wANf1gAAAAAAAAAAAAAAAAAAAAA=";
    if(!native_parse_b64_ex(data_elf,bin,&info) || !info.writes_data ||
       !info.takes_argument || info.entry_offset!=144u || info.constant!=7u ||
       info.file_size!=176u) {
        fputs("FAIL: native data-writer ELF rejected\n",stderr); return 1;
    }
    if (bin[152]!=0x20 || bin[153]!=0 || bin[154]!=0 || bin[155]!=0xb9 ||
        bin[156]!=0xc0 || bin[159]!=0xd6) {
        fputs("FAIL: wrong native write instructions\n",stderr); return 1;
    }
    char bad_data[240]; strcpy(bad_data,data_elf);
    /* Flip a byte in the Base64 encoding of the code or ELF data.
       This must not produce an accepted instruction sequence. */
    bad_data[210] = bad_data[210]=='A'?'B':'A';
    if(native_parse_b64_ex(bad_data,bin,&info)) {
        fputs("FAIL: data ELF tampering accepted\n",stderr); return 1;
    }
    puts("PASS: strict ELF64/AArch64 legacy, native C and private data-writer validation");
    return 0;
}
