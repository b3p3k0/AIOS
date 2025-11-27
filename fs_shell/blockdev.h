#ifndef AIOS_BLOCKDEV_H
#define AIOS_BLOCKDEV_H

#include <stdint.h>
#include <stddef.h>

struct blockdev {
    int fd;
    uint32_t block_size;
};

int bd_create(struct blockdev *bd, const char *path, uint32_t block_size, uint32_t total_blocks);
int bd_open(struct blockdev *bd, const char *path, uint32_t block_size);
int bd_read(struct blockdev *bd, uint32_t block, void *buf);
int bd_write(struct blockdev *bd, uint32_t block, const void *buf);
void bd_close(struct blockdev *bd);

#endif /* AIOS_BLOCKDEV_H */
