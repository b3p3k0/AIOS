#ifndef AIOS_VIRTIO_BLK_H
#define AIOS_VIRTIO_BLK_H

#include <stdint.h>
#include "fs/blockdev.h"

struct virtq_desc;
struct virtq_avail;
struct virtq_used;
struct virtio_blk_req;

struct virtio_blk {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t iobase;
    uint16_t queue_size;
    uint32_t capacity_sectors;

    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t free_head;
    uint16_t used_idx;

    struct virtio_blk_req *request;
    uint8_t *status;
};

int virtio_blk_init(struct virtio_blk *dev);
int virtio_blk_read_sectors(struct virtio_blk *dev, uint64_t lba, void *buf, uint32_t sectors);
int virtio_blk_write_sectors(struct virtio_blk *dev, uint64_t lba, const void *buf, uint32_t sectors);
int bd_init_virtio(struct blockdev *bd, struct virtio_blk *dev, uint32_t block_size);

#endif
