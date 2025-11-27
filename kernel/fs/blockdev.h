#ifndef AIOS_BLOCKDEV_H
#define AIOS_BLOCKDEV_H

#include <stdint.h>
#include <stddef.h>

struct blockdev;
typedef int (*block_read_fn)(struct blockdev *bd, uint32_t block, void *buf);
typedef int (*block_write_fn)(struct blockdev *bd, uint32_t block, const void *buf);

struct blockdev {
    void *ctx;
    uint32_t blocks;
    uint32_t block_size;
    block_read_fn read_fn;
    block_write_fn write_fn;
};

int bd_init_ram(struct blockdev *bd, void *base, uint32_t bytes, uint32_t block_size);
int bd_read(struct blockdev *bd, uint32_t block, void *buf);
int bd_write(struct blockdev *bd, uint32_t block, const void *buf);

#endif /* AIOS_BLOCKDEV_H */
