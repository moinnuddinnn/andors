#include "elf_loader.h"
#include <stddef.h>
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p+4)<<32); }
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c-'A';
    if (c >= 'a' && c <= 'z') return c-'a'+26;
    if (c >= '0' && c <= '9') return c-'0'+52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static bool decode(const char *s, uint8_t out[NATIVE_ELF_SIZE]) {
    /* 128 bytes -> 172 chars (42 full groups plus final 'xxx='). */
    for (unsigned k = 0; k < 43u; ++k) {
        unsigned off = k*4u, idx=k*3u;
        int a=b64val(s[off]), b=b64val(s[off+1]);
        if (a<0 || b<0) return false;
        if (k==42u) {
            int c=b64val(s[off+2]);
            if (c<0 || s[off+3]!='=' || s[off+4]!='\0' || (c&3)) return false;
            out[idx] = (uint8_t)((a<<2)|(b>>4));
            out[idx+1] = (uint8_t)((b<<4)|(c>>2));
        } else {
            int c=b64val(s[off+2]), d=b64val(s[off+3]);
            if (c<0 || d<0) return false;
            out[idx]=(uint8_t)((a<<2)|(b>>4));
            out[idx+1]=(uint8_t)((b<<4)|(c>>2));
            out[idx+2]=(uint8_t)((c<<6)|d);
        }
    }
    return true;
}
bool native_parse_b64(const char *b64, uint8_t dest[NATIVE_ELF_SIZE], uint32_t *return_value) {
    /* Bound strlen to avoid reading beyond a corrupted FS mailbox. */
    unsigned n=0;
    while (n <= 172u && b64[n] != '\0') ++n;
    if (n != 172u || !decode(b64,dest)) return false;
    const uint8_t *e=dest;
    if (e[0]!=0x7f || e[1]!='E' || e[2]!='L' || e[3]!='F' ||
        e[4]!=2 || e[5]!=1 || e[6]!=1 || e[7]!=0) return false;
    /* ELF64 ET_DYN AArch64, current version, no relocations, one PT_LOAD.
     * Require entry at 120 and the segment at VA 0; copying the 128-byte
     * image into the executable page resolves this restricted image. */
    if (le16(e+16)!=3 || le16(e+18)!=183 || le32(e+20)!=1 ||
        le64(e+24)!=NATIVE_ENTRY || le64(e+32)!=64 || le64(e+40)!=0 ||
        le16(e+52)!=64 || le16(e+54)!=56 || le16(e+56)!=1 ||
        le16(e+58)!=0 || le16(e+60)!=0 || le16(e+62)!=0) return false;
    const uint8_t *ph=e+64;
    if (le32(ph)!=1 || le32(ph+4)!=5 || le64(ph+8)!=0 ||
        le64(ph+16)!=0 || le64(ph+24)!=0 ||
        le64(ph+32)!=NATIVE_ELF_SIZE || le64(ph+40)!=NATIVE_ELF_SIZE ||
        le64(ph+48)!=16) return false;
    /* Allow only 'movz w0, #imm16; ret' to keep the demonstration bounded
     * and prevent infinite loops/undefined native actions. */
    uint32_t move=le32(e+120), ret=le32(e+124);
    if ((move & 0xffe0001fu)!=0x52800000u || ret!=0xd65f03c0u) return false;
    if (return_value) *return_value=(move>>5)&0xffffu;
    return true;
}
