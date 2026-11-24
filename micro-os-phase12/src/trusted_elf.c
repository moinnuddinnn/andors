#include "trusted_elf.h"
static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t u64(const uint8_t *p) { return u32(p) | ((uint64_t)u32(p+4)<<32); }
bool trusted_elf_valid(const uint8_t *e, unsigned n) {
    if(n<TRUSTED_ENTRY+4 || n>TRUSTED_ELF_MAX) return false;
    if(e[0]!=0x7f || e[1]!='E' || e[2]!='L' || e[3]!='F' || e[4]!=2 || e[5]!=1 || e[6]!=1 || e[7]!=0) return false;
    if(u16(e+16)!=3 || u16(e+18)!=183 || u32(e+20)!=1 || u64(e+24)!=TRUSTED_ENTRY || u64(e+32)!=64 || u64(e+40)!=0 || u32(e+48)!=0 || u16(e+52)!=64 || u16(e+54)!=56 || u16(e+56)!=1 || u16(e+58)!=0 || u16(e+60)!=0 || u16(e+62)!=0) return false;
    const uint8_t *p=e+64;
    if(u32(p)!=1 || u32(p+4)!=5 || u64(p+8)!=0 || u64(p+16)!=0 || u64(p+24)!=0 || u64(p+32)!=n || u64(p+40)!=n || u64(p+48)!=16) return false;
    for(unsigned i=120;i<TRUSTED_ENTRY;i++) if(e[i]) return false;
    return true;
}
static int digit(char c) {
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a'+26;
    if(c>='0'&&c<='9') return c-'0'+52;
    return c=='+'?62:c=='/'?63:-1;
}
bool trusted_chunk_decode(const char *s,uint8_t *out,unsigned bytes) {
    if(!bytes||bytes>TRUSTED_CHUNK) return false;
    unsigned chars=4*((bytes+2)/3), n=0;
    while(n<=chars && s[n]) ++n;
    if(n!=chars) return false;
    unsigned o=0;
    for(unsigned i=0;i<chars;i+=4) {
        int a=digit(s[i]),b=digit(s[i+1]);
        bool p=s[i+2]=='=',q=s[i+3]=='=';
        int c=p?0:digit(s[i+2]),d=q?0:digit(s[i+3]);
        if(a<0||b<0||c<0||d<0||(p&&!q)||((p||q)&&i+4!=chars)||(p&&(b&15))||(q&&!p&&(c&3))) return false;
        if(o>=bytes) return false;
        out[o++]=(uint8_t)((a<<2)|(b>>4));
        if(!p){if(o>=bytes)return false;out[o++]=(uint8_t)((b<<4)|(c>>2));}
        if(!q){if(o>=bytes)return false;out[o++]=(uint8_t)((c<<6)|d);}
    }
    return o==bytes;
}
uint32_t trusted_crc32(const uint8_t *p,unsigned n) {
    uint32_t c=~0u;
    while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
    return ~c;
}
