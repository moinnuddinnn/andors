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
    const char *abi_b64="f0VMRgIBAQAAAAAAAAAAAAMAtwABAAAAiAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAEAAOAABAAAAAAAAAAEAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAuAAAAAAAAAC4AAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAP17vqnzCwD5/QMAkfMDACogAD/W6ACAUmAiExvzC0D5/XvCqMADX9YAAAAAAAAAAA==";
    if(!native_parse_b64_ex(abi_b64,bin,&info) || !info.uses_console ||
       info.file_size!=184u || info.entry_offset!=NATIVE_ABI_ENTRY ||
       info.constant!=7u || !info.takes_argument || info.writes_data) {
        fputs("FAIL: v1.0 ABI C ELF rejected\n",stderr); return 1;
    }
    char altered[260]; strcpy(altered,abi_b64);
    altered[210]=(altered[210]=='A'?'B':'A');
    if (native_parse_b64_ex(altered,bin,&info)) {
        fputs("FAIL: tampered native callback ABI accepted\n",stderr); return 1;
    }

    /* v1.1: actual Clang -Oz C file calls two distinct console operations.
     * A single 184-byte binary is still small enough for MicroFS. */
    const char *multi_b64="f0VMRgIBAQAAAAAAAAAAAAMAtwABAAAAfAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAEAAOAABAAAAAAAAAAEAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAuAAAAAAAAAC4AAAAAAAAABAAAAAAAAAAAAAAAP17vqn0TwGp9AMAKvMDAaogAIBS4QMUKv0DAJFgAj/WQACAUuEDFCpgAj/W4AMUKvRPQan9e8KowANf1g==";
    if(!native_parse_b64_ex(multi_b64,bin,&info) || !info.uses_multi_console ||
       !info.uses_console || info.entry_offset!=NATIVE_MULTI_ENTRY ||
       info.file_size!=184u || !info.takes_argument) {
        fputs("FAIL: v1.1 two-service ABI ELF rejected\n",stderr); return 1;
    }
    char tampered_multi[260]; strcpy(tampered_multi,multi_b64);
    tampered_multi[210]=(tampered_multi[210]=='A'?'B':'A');
    if(native_parse_b64_ex(tampered_multi,bin,&info)) {
        fputs("FAIL: modified v1.1 native ELF accepted\n",stderr); return 1;
    }
    puts("PASS: ELF64/AArch64 legacy, C, private data, and DEC/HEX ABI validation");
    return 0;
}
