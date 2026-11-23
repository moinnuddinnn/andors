/* Micro-OS v0.4: isolated VirtIO 1.0+ MMIO block driver for QEMU virt AArch64.
 * - Older QEMU virt exposes 32 VirtIO-MMIO transports by default; the first
 *   automatically attached virtio-blk-device gets base 0x0a003e00, GIC INTID 79.
 *   Microkit maps the 4 KiB page at 0x0a003000; registers start at +0xe00.
 *   This assumes exactly one VirtIO-MMIO device on the QEMU command line.
 * - Uses one outstanding 512-byte request and a split virtqueue with 8 slots.
 * - DMA buffers are in a physically contiguous memory region assigned by
 *   Microkit and made available through setvar region_paddr.
 * - Emulator-only: assumes QEMU's documented dma-coherent virt platform;
 *   physical devices require a real DMA/cache-maintenance policy.
 */
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "block_protocol.h"

#define CH_FILESYSTEM 0u
#define CH_IRQ 1u
#define VQ_NUM 8u
#define MMIO_TRANSPORT_OFFSET 0xe00u
#define MMIO_MAGIC 0x000u
#define MMIO_VERSION 0x004u
#define MMIO_DEVICE_ID 0x008u
#define MMIO_DEVICE_FEATURES 0x010u
#define MMIO_DEVICE_FEATURES_SEL 0x014u
#define MMIO_DRIVER_FEATURES 0x020u
#define MMIO_DRIVER_FEATURES_SEL 0x024u
#define MMIO_QUEUE_SEL 0x030u
#define MMIO_QUEUE_NUM_MAX 0x034u
#define MMIO_QUEUE_NUM 0x038u
#define MMIO_QUEUE_READY 0x044u
#define MMIO_QUEUE_NOTIFY 0x050u
#define MMIO_INTERRUPT_STATUS 0x060u
#define MMIO_INTERRUPT_ACK 0x064u
#define MMIO_STATUS 0x070u
#define MMIO_QUEUE_DESC_LOW 0x080u
#define MMIO_QUEUE_DESC_HIGH 0x084u
#define MMIO_QUEUE_DRIVER_LOW 0x090u
#define MMIO_QUEUE_DRIVER_HIGH 0x094u
#define MMIO_QUEUE_DEVICE_LOW 0x0a0u
#define MMIO_QUEUE_DEVICE_HIGH 0x0a4u
#define MMIO_CONFIG_CAPACITY_LOW 0x100u
#define MMIO_CONFIG_CAPACITY_HIGH 0x104u
#define VIRTIO_MAGIC 0x74726976u
#define VIRTIO_BLK_ID 2u
#define VIRTIO_STATUS_ACK 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_DRIVER_OK 4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u
#define DMA_DESC 0x0000u
#define DMA_AVAIL 0x1000u
#define DMA_USED 0x2000u
#define DMA_HEADER 0x3000u
#define DMA_STATUS 0x3010u
#define DMA_BLOCK 0x4000u
#define DMA_PAGES 0x5000u

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;
typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VQ_NUM];
    uint16_t used_event;
} virtq_avail_t;
typedef struct {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;
typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VQ_NUM];
    uint16_t avail_event;
} virtq_used_t;
typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} virtio_blk_header_t;

_Static_assert(sizeof(virtq_desc_t)==16, "virtq descriptor must be 16 bytes");
_Static_assert(sizeof(virtio_blk_header_t)==16, "block header must be 16 bytes");

uintptr_t virtio_mmio_vaddr = 0;
uintptr_t block_mailbox_vaddr = 0;
uintptr_t blk_dma_vaddr = 0;
uintptr_t blk_dma_paddr = 0;

static bool device_ready = false;
static bool request_busy = false;
static uint32_t inflight_seq = 0;
static uint32_t processed_seq = 0;
static uint32_t inflight_op = 0;
static uint16_t seen_used_idx = 0;
static uint16_t next_avail_idx = 0;
static uint64_t capacity_sectors = 0;

static inline uint32_t mmio_read(uint32_t off)
{
    return *(volatile uint32_t *)(virtio_mmio_vaddr + MMIO_TRANSPORT_OFFSET + off);
}
static inline void mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(virtio_mmio_vaddr + MMIO_TRANSPORT_OFFSET + off) = val;
}
static inline uint64_t dma_phys(unsigned offset)
{
    return (uint64_t)blk_dma_paddr + offset;
}
static inline void *dma_virt(unsigned offset)
{
    return (void *)(blk_dma_vaddr + offset);
}
static inline void dma_clear(void)
{
    volatile uint8_t *p=(volatile uint8_t *)dma_virt(0);
    for (unsigned i=0;i<DMA_PAGES;++i) p[i]=0;
}
static inline void copy_bytes(void *dst, const void *src, unsigned n)
{
    uint8_t *d=(uint8_t *)dst;
    const uint8_t *s=(const uint8_t *)src;
    for(unsigned i=0;i<n;++i) d[i]=s[i];
}
static inline void store_desc(virtq_desc_t *desc, uint64_t addr,
                              uint32_t len, uint16_t flags, uint16_t next)
{
    desc->addr = addr;
    desc->len = len;
    desc->flags = flags;
    desc->next = next;
}
static void fail_request(uint32_t status)
{
    block_mailbox_t *mb=(block_mailbox_t *)block_mailbox_vaddr;
    mb->status=status;
    __atomic_store_n(&mb->response_seq, inflight_seq, __ATOMIC_RELEASE);
    processed_seq=inflight_seq;
    request_busy=false;
    microkit_notify(CH_FILESYSTEM);
}
static void dbg_hex32(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char repr[] = "0x00000000";
    for (unsigned shift = 0; shift < 8u; ++shift) {
        repr[2u + shift] = digits[(value >> (28u - shift * 4u)) & 0xfu];
    }
    microkit_dbg_puts(repr);
}
static void initialize_device(void)
{
    block_mailbox_t *mb=(block_mailbox_t *)block_mailbox_vaddr;
    /* These three registers are read-only in both VirtIO-MMIO versions.
     * Inspect them independently before resetting the hardware. */
    const uint32_t magic = mmio_read(MMIO_MAGIC);
    const uint32_t version = mmio_read(MMIO_VERSION);
    const uint32_t device_id = mmio_read(MMIO_DEVICE_ID);
    microkit_dbg_puts("[BLK] Probe @ phys 0x0a003e00: MAGIC=");
    dbg_hex32(magic);
    microkit_dbg_puts(" VERSION=");
    dbg_hex32(version);
    microkit_dbg_puts(" DEVICE_ID=");
    dbg_hex32(device_id);
    microkit_dbg_puts("\n");
    if (magic != VIRTIO_MAGIC) {
        microkit_dbg_puts("[BLK] ERROR: missing VirtIO magic: wrong MMIO address or transport mapping\n");
        return;
    }
    if (device_id != VIRTIO_BLK_ID) {
        microkit_dbg_puts("[BLK] ERROR: this transport has no block device (expected ID 2)\n");
        return;
    }
    if (version != 2u) {
        if (version == 1u)
            microkit_dbg_puts("[BLK] ERROR: legacy VirtIO-MMIO v1; add -global virtio-mmio.force-legacy=false\n");
        else
            microkit_dbg_puts("[BLK] ERROR: unsupported VirtIO-MMIO version\n");
        return;
    }
    mmio_write(MMIO_STATUS,0);
    mmio_write(MMIO_STATUS,VIRTIO_STATUS_ACK);
    mmio_write(MMIO_STATUS,VIRTIO_STATUS_ACK|VIRTIO_STATUS_DRIVER);
    mmio_write(MMIO_DEVICE_FEATURES_SEL,1);
    uint32_t high=mmio_read(MMIO_DEVICE_FEATURES);
    if((high&1u)==0u){
        microkit_dbg_puts("[BLK] VIRTIO_F_VERSION_1 not offered\n");
        return;
    }
    /* Negotiate only mandatory VIRTIO_F_VERSION_1, no optional block features. */
    mmio_write(MMIO_DRIVER_FEATURES_SEL,0);
    mmio_write(MMIO_DRIVER_FEATURES,0);
    mmio_write(MMIO_DRIVER_FEATURES_SEL,1);
    mmio_write(MMIO_DRIVER_FEATURES,1);
    mmio_write(MMIO_STATUS,VIRTIO_STATUS_ACK|VIRTIO_STATUS_DRIVER|VIRTIO_STATUS_FEATURES_OK);
    if((mmio_read(MMIO_STATUS)&VIRTIO_STATUS_FEATURES_OK)==0u){
        microkit_dbg_puts("[BLK] VirtIO feature negotiation failed\n");
        return;
    }
    mmio_write(MMIO_QUEUE_SEL,0);
    if(mmio_read(MMIO_QUEUE_NUM_MAX)<VQ_NUM){
        microkit_dbg_puts("[BLK] VirtIO queue too small\n");
        return;
    }
    dma_clear();
    mmio_write(MMIO_QUEUE_NUM,VQ_NUM);
    mmio_write(MMIO_QUEUE_DESC_LOW,(uint32_t)dma_phys(DMA_DESC));
    mmio_write(MMIO_QUEUE_DESC_HIGH,(uint32_t)(dma_phys(DMA_DESC)>>32));
    mmio_write(MMIO_QUEUE_DRIVER_LOW,(uint32_t)dma_phys(DMA_AVAIL));
    mmio_write(MMIO_QUEUE_DRIVER_HIGH,(uint32_t)(dma_phys(DMA_AVAIL)>>32));
    mmio_write(MMIO_QUEUE_DEVICE_LOW,(uint32_t)dma_phys(DMA_USED));
    mmio_write(MMIO_QUEUE_DEVICE_HIGH,(uint32_t)(dma_phys(DMA_USED)>>32));
    mmio_write(MMIO_QUEUE_READY,1);
    if(mmio_read(MMIO_QUEUE_READY)!=1u){
        microkit_dbg_puts("[BLK] VirtIO queue could not become ready\n");
        return;
    }
    capacity_sectors=((uint64_t)mmio_read(MMIO_CONFIG_CAPACITY_HIGH)<<32)
                     |mmio_read(MMIO_CONFIG_CAPACITY_LOW);
    mb->capacity=capacity_sectors;
    if(capacity_sectors<128u){
        microkit_dbg_puts("[BLK] virtual disk smaller than 64 KiB\n");
        return;
    }
    mmio_write(MMIO_STATUS,VIRTIO_STATUS_ACK|VIRTIO_STATUS_DRIVER|
               VIRTIO_STATUS_FEATURES_OK|VIRTIO_STATUS_DRIVER_OK);
    device_ready=true;
    microkit_dbg_puts("[BLK] VirtIO MMIO block driver online; modern split ring\n");
}
static void submit_request(void)
{
    block_mailbox_t *mb=(block_mailbox_t *)block_mailbox_vaddr;
    uint32_t seq=__atomic_load_n(&mb->request_seq,__ATOMIC_ACQUIRE);
    if(seq==processed_seq || (request_busy && seq==inflight_seq)) return;
    if(request_busy){
        /* FS respects a single outstanding request, so this shouldn't occur. */
        return;
    }
    inflight_seq=seq;
    if(!device_ready){fail_request(BLOCK_ERR_NO_DEVICE);return;}
    if(mb->sector>=capacity_sectors){fail_request(BLOCK_ERR_RANGE);return;}
    if(mb->operation!=BLOCK_READ && mb->operation!=BLOCK_WRITE){
        fail_request(BLOCK_ERR_IO);return;
    }
    inflight_op=mb->operation;
    request_busy=true;
    virtio_blk_header_t *h=(virtio_blk_header_t *)dma_virt(DMA_HEADER);
    h->type=(inflight_op==BLOCK_READ)?0u:1u;
    h->reserved=0;
    h->sector=mb->sector;
    volatile uint8_t *status=(volatile uint8_t *)dma_virt(DMA_STATUS);
    *status=0xffu;
    if(inflight_op==BLOCK_WRITE) copy_bytes(dma_virt(DMA_BLOCK),mb->data,BLOCK_SIZE);
    virtq_desc_t *desc=(virtq_desc_t *)dma_virt(DMA_DESC);
    store_desc(&desc[0],dma_phys(DMA_HEADER),16,VIRTQ_DESC_F_NEXT,1);
    store_desc(&desc[1],dma_phys(DMA_BLOCK),BLOCK_SIZE,
               VIRTQ_DESC_F_NEXT|((inflight_op==BLOCK_READ)?VIRTQ_DESC_F_WRITE:0u),2);
    store_desc(&desc[2],dma_phys(DMA_STATUS),1,VIRTQ_DESC_F_WRITE,0);
    virtq_avail_t *avail=(virtq_avail_t *)dma_virt(DMA_AVAIL);
    avail->ring[next_avail_idx%VQ_NUM]=0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    ++next_avail_idx;
    __atomic_store_n(&avail->idx,next_avail_idx,__ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    mmio_write(MMIO_QUEUE_NOTIFY,0);
}
static void complete_irq(void)
{
    uint32_t pending=mmio_read(MMIO_INTERRUPT_STATUS);
    mmio_write(MMIO_INTERRUPT_ACK,pending);
    virtq_used_t *used=(virtq_used_t *)dma_virt(DMA_USED);
    uint16_t idx=__atomic_load_n(&used->idx,__ATOMIC_ACQUIRE);
    if(request_busy && idx!=seen_used_idx){
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        uint32_t completed_id=used->ring[seen_used_idx%VQ_NUM].id;
        seen_used_idx=idx;
        block_mailbox_t *mb=(block_mailbox_t *)block_mailbox_vaddr;
        uint8_t status=*(volatile uint8_t *)dma_virt(DMA_STATUS);
        mb->status=(completed_id==0u && status==0u)?BLOCK_OK:BLOCK_ERR_IO;
        if(mb->status==BLOCK_OK && inflight_op==BLOCK_READ){
            copy_bytes(mb->data,dma_virt(DMA_BLOCK),BLOCK_SIZE);
        }
        __atomic_store_n(&mb->response_seq,inflight_seq,__ATOMIC_RELEASE);
        processed_seq=inflight_seq;
        request_busy=false;
        microkit_notify(CH_FILESYSTEM);
    }
    microkit_irq_ack(CH_IRQ);
}
void init(void)
{
    initialize_device();
}
void notified(microkit_channel channel)
{
    if(channel==CH_FILESYSTEM) submit_request();
    else if(channel==CH_IRQ) complete_irq();
}
