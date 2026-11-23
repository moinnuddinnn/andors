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
/* Strict canonical Base64: no whitespace; no over-reads; exact image length. */
static bool decode(const char *s, unsigned chars, uint8_t out[NATIVE_ELF_SIZE], unsigned bytes) {
    unsigned o = 0;
    for (unsigned i=0; i<chars; i+=4) {
        int a=b64val(s[i]), b=b64val(s[i+1]);
        if (a<0 || b<0) return false;
        bool cpad = s[i+2]=='=', dpad=s[i+3]=='=';
        if (cpad && !dpad) return false;
        if ((cpad || dpad) && i+4!=chars) return false;
        int c=cpad?0:b64val(s[i+2]), d=dpad?0:b64val(s[i+3]);
        if (c<0 || d<0) return false;
        if ((cpad && (b&15)) || (dpad && !cpad && (c&3))) return false;
        if (o>=bytes) return false;
        out[o++]=(uint8_t)((a<<2)|(b>>4));
        if (!cpad) {
            if (o>=bytes) return false;
            out[o++]=(uint8_t)((b<<4)|(c>>2));
        }
        if (!dpad) {
            if (o>=bytes) return false;
            out[o++]=(uint8_t)((c<<6)|d);
        }
    }
    return o==bytes;
}
static bool all_zero(const uint8_t *buf, unsigned from, unsigned until) {
    for (unsigned i=from;i<until;++i) if(buf[i]) return false;
    return true;
}
bool native_parse_b64_ex(const char *b64, uint8_t dest[NATIVE_ELF_SIZE], native_image_info_t *info) {
    /* 128 bytes -> 172 Base64 chars, 160 bytes -> 216. */
    unsigned n=0;
    while (n<=216u && b64[n]!='\0') ++n;
    const bool legacy=n==172u, cprog=n==216u;
    if (!legacy && !cprog) return false;
    const unsigned sz=legacy?128u:160u;
    for (unsigned i=0;i<NATIVE_ELF_SIZE;++i) dest[i]=0;
    if(!decode(b64,n,dest,sz)) return false;
    const uint8_t *e=dest;
    if (e[0]!=0x7f || e[1]!='E' || e[2]!='L' || e[3]!='F' ||
        e[4]!=2 || e[5]!=1 || e[6]!=1 || e[7]!=0) return false;
    const uint32_t entry=legacy?NATIVE_ENTRY:NATIVE_C_ENTRY;
    if (le16(e+16)!=3 || le16(e+18)!=183 || le32(e+20)!=1 ||
        le64(e+24)!=entry || le64(e+32)!=64 || le64(e+40)!=0 ||
        le16(e+52)!=64 || le16(e+54)!=56 || le16(e+56)!=1 ||
        le16(e+58)!=0 || le16(e+60)!=0 || le16(e+62)!=0) return false;
    const uint8_t *ph=e+64;
    if (le32(ph)!=1 || le32(ph+4)!=5 || le64(ph+8)!=0 ||
        le64(ph+16)!=0 || le64(ph+24)!=0 ||
        le64(ph+32)!=sz || le64(ph+40)!=sz || le64(ph+48)!=16) return false;
    /* Reject hidden code in the gap/padding; we never execute it. */
    if (cprog && (!all_zero(e,120,128) || !all_zero(e,140,160))) return false;
    const uint32_t a=le32(e+entry), b=le32(e+entry+4);
    if(legacy) {
        if((a&0xffe0001fu)!=0x52800000u || b!=0xd65f03c0u) return false;
        if(info) *info=(native_image_info_t){entry,sz,(a>>5)&65535u,false};
    } else {
        /* C compilation of: uint32_t program(uint32_t x) { return x*x + K; }
         * Allowed machine code: movz wN,#K ; madd w0,w0,w0,wN ; ret.
         * No loads, stores, branches or OS calls; bounded pure function. */
        unsigned reg=a&31u;
        if((a&0xffe00000u)!=0x52800000u || reg==31u ||
           b!=(0x1b000000u | (reg<<10)) ||
           le32(e+entry+8)!=0xd65f03c0u) return false;
        if(info) *info=(native_image_info_t){entry,sz,(a>>5)&65535u,true};
    }
    return true;
}
bool native_parse_b64(const char *s, uint8_t dest[NATIVE_ELF_SIZE], uint32_t *constant) {
    native_image_info_t i;
    if(!native_parse_b64_ex(s,dest,&i)) return false;
    if(constant) *constant=i.constant;
    return true;
}
