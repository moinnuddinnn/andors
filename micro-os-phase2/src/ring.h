#ifndef MICRO_OS_RING_H
#define MICRO_OS_RING_H

/* A simple single-producer, single-consumer ring in a shared RAM page.
 * Each direction has its own ring: UART->terminal and terminal->UART.
 * Each endpoint writes only its own index.  Notifications are wake-ups,
 * not counted messages; the consumer always drains the entire ring.
 */
#include <stdint.h>
#include <stdbool.h>

#define RING_CAPACITY 2048u  /* power of two; total struct fits one 4-KiB page */

typedef struct {
    uint32_t head;  /* incremented by producer */
    uint32_t tail;  /* incremented by consumer */
    uint8_t  data[RING_CAPACITY];
} ring_t;

static inline bool ring_push(ring_t *ring, uint8_t byte)
{
    uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
    if ((uint32_t)(head - tail) >= RING_CAPACITY) {
        return false;
    }
    ring->data[head & (RING_CAPACITY - 1u)] = byte;
    __atomic_store_n(&ring->head, head + 1u, __ATOMIC_RELEASE);
    return true;
}

static inline bool ring_pop(ring_t *ring, uint8_t *byte)
{
    uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    *byte = ring->data[tail & (RING_CAPACITY - 1u)];
    __atomic_store_n(&ring->tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

#endif
