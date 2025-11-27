#ifndef AIOS_KERNEL_SHELL_H
#define AIOS_KERNEL_SHELL_H

#include <stdbool.h>
#include "fs/fs.h"
#include "virtio_blk.h"

struct storage_state {
    fs_t fs;
    struct blockdev ram_dev;
    struct blockdev virtio_dev;
    struct blockdev *active_dev;
    struct virtio_blk virtio;
    bool virtio_present;
    bool fs_ready;
    bool needs_format;
    bool using_ram;
    bool ram_seed_present;
    uint32_t ram_seed_blocks;
    uint32_t ram_seed_block_size;
};

struct shell_env {
    struct storage_state *storage;
    const struct aios_boot_info *boot;
};

void shell_run(struct shell_env *env);

#endif
