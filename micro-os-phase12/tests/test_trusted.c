#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "trusted_elf.h"
#include "app_protocol.h"
extern uintptr_t trusted_exec_vaddr,trusted_mailbox_vaddr;
void init(void); void notified(int);
static int events;
void microkit_notify(int ch){assert(ch==0);events++;}
void microkit_dbg_puts(const char *s){(void)s;}
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint8_t data[TRUSTED_ELF_MAX];unsigned n=fread(data,1,sizeof(data),f);fclose(f);
    assert(n>=132&&trusted_elf_valid(data,n));assert(!trusted_elf_valid(data,n-1));
    data[68]=7;assert(!trusted_elf_valid(data,n));data[68]=5;
    assert(trusted_elf_valid(data,n));assert(trusted_crc32((const uint8_t*)"123456789",9)==0xcbf43926u);
    uint8_t b[3];assert(trusted_chunk_decode("AQID",b,3)&&b[0]==1&&b[1]==2&&b[2]==3);
    assert(trusted_chunk_decode("AQI=",b,2));assert(!trusted_chunk_decode("AQJ=",b,2));assert(!trusted_chunk_decode("AQID",b,2));
    app_worker_mailbox_t m={0};trusted_mailbox_vaddr=(uintptr_t)&m;trusted_exec_vaddr=(uintptr_t)data;init();
    snprintf(m.args,sizeof(m.args),"%u 9",n);m.request_seq=1;notified(0);assert(events==1&&m.response_seq==1&&strstr(m.reply,"requires AArch64"));
    notified(0);assert(events==1);strcpy(m.args,"2049 9");m.request_seq=2;notified(0);assert(events==2&&strstr(m.reply,"invalid image"));
    puts("PASS: trusted ELF structure, canonical Base64, CRC, worker validation");return 0;
}
