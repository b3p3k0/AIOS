#include "blockdev.h"
#include "util.h"
#include "mem.h"

struct ram_ctx {
    uint8_t *base;
};

static int ram_read(struct blockdev *bd, uint32_t block, void *buf) {
    if (block >= bd->blocks) return -1;
    struct ram_ctx *rc = (struct ram_ctx *)bd->ctx;
    memcpy(buf, rc->base + ((size_t)block * bd->block_size), bd->block_size);
    return 0;
}

static int ram_write(struct blockdev *bd, uint32_t block, const void *buf) {
    if (block >= bd->blocks) return -1;
    struct ram_ctx *rc = (struct ram_ctx *)bd->ctx;
    memcpy(rc->base + ((size_t)block * bd->block_size), buf, bd->block_size);
    return 0;
}

int bd_init_ram(struct blockdev *bd, void *base, uint32_t bytes, uint32_t block_size) {
    if (block_size == 0 || bytes < block_size) return -1;
    struct ram_ctx *ctx = (struct ram_ctx *)kalloc(sizeof(struct ram_ctx));
    if (!ctx) return -1;
    ctx->base = (uint8_t *)base;
    bd->ctx = ctx;
    bd->block_size = block_size;
    bd->blocks = bytes / block_size;
    bd->read_fn = ram_read;
    bd->write_fn = ram_write;
    return 0;
}

int bd_read(struct blockdev *bd, uint32_t block, void *buf) {
    if (!bd->read_fn) return -1;
    if (block >= bd->blocks) return -1;
    return bd->read_fn(bd, block, buf);
}

int bd_write(struct blockdev *bd, uint32_t block, const void *buf) {
    if (!bd->write_fn) return -1;
    if (block >= bd->blocks) return -1;
    return bd->write_fn(bd, block, buf);
}
