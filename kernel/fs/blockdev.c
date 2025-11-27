#include "blockdev.h"
#include "util.h"

int bd_init_ram(struct blockdev *bd, void *base, uint32_t bytes, uint32_t block_size) {
    if (block_size == 0 || bytes < block_size) {
        return -1;
    }
    bd->base = (uint8_t *)base;
    bd->block_size = block_size;
    bd->blocks = bytes / block_size;
    return 0;
}

int bd_read(struct blockdev *bd, uint32_t block, void *buf) {
    if (block >= bd->blocks) {
        return -1;
    }
    memcpy(buf, bd->base + ((size_t)block * bd->block_size), bd->block_size);
    return 0;
}

int bd_write(struct blockdev *bd, uint32_t block, const void *buf) {
    if (block >= bd->blocks) {
        return -1;
    }
    memcpy(bd->base + ((size_t)block * bd->block_size), buf, bd->block_size);
    return 0;
}
