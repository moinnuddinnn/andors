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
    puts("PASS: strict base64 ELF64/AArch64 single-segment return-function validation");
    return 0;
}
