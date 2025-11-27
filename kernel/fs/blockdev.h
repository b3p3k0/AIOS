#ifndef AIOS_BLOCKDEV_H
#define AIOS_BLOCKDEV_H

#include <stdint.h>
#include <stddef.h>

/* In-kernel block device abstraction.
 * For Phase 1.5 we back this with a RAM disk passed from the loader.
 */
struct blockdev {
    uint8_t *base;      /* start of disk image in RAM */
    uint32_t blocks;    /* number of blocks available */
    uint32_t block_size;
};

int bd_init_ram(struct blockdev *bd, void *base, uint32_t bytes, uint32_t block_size);
int bd_read(struct blockdev *bd, uint32_t block, void *buf);
int bd_write(struct blockdev *bd, uint32_t block, const void *buf);

#endif /* AIOS_BLOCKDEV_H */
