#ifndef AIOS_BOOTINFO_H
#define AIOS_BOOTINFO_H

#include <stdint.h>

#define AIOS_BOOTINFO_MAGIC 0x41494f53424f4f54ULL /* "AIOSBOOT" */
#define AIOS_BOOTINFO_VERSION 1

struct aios_framebuffer {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t bpp;
};

struct aios_memory_map {
    uint64_t buffer;       /* physical address */
    uint64_t size;         /* bytes */
    uint64_t descriptor_size;
    uint32_t descriptor_version;
};

struct aios_boot_info {
    uint64_t magic;
    uint64_t version;
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t entry_point;
    uint64_t rsdp_address;
    struct aios_framebuffer framebuffer;
    struct aios_memory_map memory_map;
};

#endif /* AIOS_BOOTINFO_H */
