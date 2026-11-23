/* Basic unit test of REAL guest VirtIO-MMIO driver against simulated MMIO and
 * virtqueue memory. Does not emulate QEMU or the GIC; validates descriptor
 * construction, DMA address wiring, completion handling, and block mailbox.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "block_protocol.h"

extern uintptr_t virtio_mmio_vaddr, block_mailbox_vaddr, blk_dma_vaddr, blk_dma_paddr;
extern void init(void);
extern void notified(int);
static uint8_t mmio[4096] __attribute__((aligned(4096)));
static uint8_t dma[0x5000] __attribute__((aligned(4096)));
static block_mailbox_t mailbox;
static unsigned notifications,acks;
void microkit_notify(int channel) {if(channel!=0)abort();++notifications;}
void microkit_irq_ack(int channel) {if(channel!=1)abort();++acks;}
void microkit_dbg_puts(const char *message) {(void)message;}
typedef struct {uint64_t addr;uint32_t len;uint16_t flags,next;} desc_t;
typedef struct {uint32_t id,len;} used_elem_t;
typedef struct {uint16_t flags,idx;used_elem_t ring[8];} used_t;
/* The QEMU transport occupies offsets 0xe00..0xfff in its 4 KiB page. */
static uint32_t *reg(unsigned off) {return (uint32_t *)(mmio+0xe00u+off);}
static void ensure(bool x,const char *why){if(!x){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(void)
{
    *reg(0x000)=0x74726976u;
    *reg(0x004)=2;
    *reg(0x008)=2;
    *reg(0x010)=1; /* high word: VERSION_1 */
    *reg(0x034)=256;
    *reg(0x100)=16384;
    virtio_mmio_vaddr=(uintptr_t)mmio;
    block_mailbox_vaddr=(uintptr_t)&mailbox;
    blk_dma_vaddr=(uintptr_t)dma;
    blk_dma_paddr=(uintptr_t)0x40006000;
    /* Regression: QEMU legacy mode reports version 1 even with valid magic/ID.
     * That is an incompatible mode, not proof that the device is absent. */
    *reg(0x004)=1;
    init();
    ensure(mailbox.capacity==0,"legacy VirtIO-MMIO v1 must not be used by modern driver");
    *reg(0x004)=2;
    init();
    ensure(mailbox.capacity==16384,"device capacity");
    ensure(*reg(0x044)==1,"queue enabled");
    ensure(*reg(0x080)==0x40006000,"descriptor physical address");
    ensure(*reg(0x090)==0x40007000,"available ring physical address");
    ensure(*reg(0x0a0)==0x40008000,"used ring physical address");

    mailbox.operation=BLOCK_READ;
    mailbox.sector=5;
    mailbox.request_seq=1;
    notified(0);
    desc_t *d=(desc_t *)dma;
    ensure(d[0].addr==0x40009000 && d[0].len==16 && d[0].next==1,"request header descriptor");
    ensure(d[1].addr==0x4000a000 && d[1].len==512 && d[1].flags==3,"read data descriptor writable");
    ensure(d[2].addr==0x40009010 && d[2].len==1 && d[2].flags==2,"status descriptor writable");
    ensure(*(uint32_t *)(dma+0x3000)==0,"virtio block read opcode");
    ensure(*(uint64_t *)(dma+0x3008)==5,"sector number");
    memset(dma+0x4000,'X',512);
    dma[0x3010]=0;
    used_t *used=(used_t *)(dma+0x2000);
    used->ring[0].id=0;
    used->idx=1;
    *reg(0x060)=1;
    notified(1);
    ensure(mailbox.response_seq==1 && mailbox.status==BLOCK_OK,"read completion");
    ensure(mailbox.data[0]=='X' && mailbox.data[511]=='X',"read copy back");
    ensure(notifications==1 && acks==1,"read notifications and IRQ acknowledgement");

    mailbox.operation=BLOCK_WRITE;
    mailbox.sector=6;
    memset(mailbox.data,'Y',512);
    mailbox.request_seq=2;
    notified(0);
    ensure(d[1].flags==1,"write payload descriptor must not be device-writable");
    ensure(*(uint32_t *)(dma+0x3000)==1,"virtio block write opcode");
    ensure(dma[0x4000]=='Y' && dma[0x41ff]=='Y',"write payload transferred into DMA");
    dma[0x3010]=0;
    used->ring[1].id=0;
    used->idx=2;
    notified(1);
    ensure(mailbox.response_seq==2 && mailbox.status==BLOCK_OK,"write completion");
    ensure(notifications==2 && acks==2,"write notifications and IRQ acknowledgement");
    puts("PASS: VirtIO feature init, DMA physical addresses, descriptor chains, read/write IRQ completions");
    return 0;
}
