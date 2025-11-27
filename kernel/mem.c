#include "mem.h"

static uint8_t *heap_base = NULL;
static size_t heap_size = 0;
static size_t heap_offset = 0;

static size_t align_up(size_t value, size_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

void mem_init(void *base, size_t bytes) {
    heap_base = (uint8_t *)base;
    heap_size = bytes;
    heap_offset = 0;
}

void *kalloc_aligned(size_t bytes, size_t alignment) {
    if (!heap_base || bytes == 0) return NULL;
    alignment = alignment ? alignment : 8;
    size_t offset = align_up(heap_offset, alignment);
    if (offset + bytes > heap_size) return NULL;
    void *ptr = heap_base + offset;
    heap_offset = offset + align_up(bytes, 8);
    return ptr;
}

void *kalloc(size_t bytes) {
    return kalloc_aligned(bytes, 8);
}

void *kcalloc(size_t count, size_t size) {
    size_t total = count * size;
    uint8_t *ptr = (uint8_t *)kalloc(total);
    if (ptr) {
        for (size_t i = 0; i < total; ++i) {
            ptr[i] = 0;
        }
    }
    return ptr;
}

void kfree(void *ptr) {
    (void)ptr;
}

size_t mem_used(void) {
    return heap_offset;
}

size_t mem_total(void) {
    return heap_size;
}
