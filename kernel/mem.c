#include "mem.h"

static uint8_t *heap_base = NULL;
static size_t heap_size = 0;
static size_t heap_offset = 0;

void mem_init(void *base, size_t bytes) {
    heap_base = (uint8_t *)base;
    heap_size = bytes;
    heap_offset = 0;
}

static size_t align_up(size_t v, size_t a) {
    return (v + (a - 1)) & ~(a - 1);
}

void *kalloc(size_t bytes) {
    if (!heap_base || bytes == 0) return NULL;
    size_t aligned = align_up(bytes, 8);
    if (heap_offset + aligned > heap_size) {
        return NULL;
    }
    void *p = heap_base + heap_offset;
    heap_offset += aligned;
    return p;
}

void *kcalloc(size_t count, size_t size) {
    size_t total = count * size;
    uint8_t *p = kalloc(total);
    if (p) {
        for (size_t i = 0; i < total; ++i) {
            p[i] = 0;
        }
    }
    return p;
}

void kfree(void *ptr) {
    (void)ptr;
    /* bump allocator: free is a no-op */
}
