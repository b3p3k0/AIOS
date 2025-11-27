#ifndef AIOS_KERNEL_MEM_H
#define AIOS_KERNEL_MEM_H

#include <stddef.h>
#include <stdint.h>

void mem_init(void *base, size_t bytes);
void *kalloc(size_t bytes);
void *kcalloc(size_t count, size_t size);
void kfree(void *ptr); /* no-op in bump allocator */

#endif
