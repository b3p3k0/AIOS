#define _XOPEN_SOURCE 700
#include "blockdev.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int bd_create(struct blockdev *bd, const char *path, uint32_t block_size, uint32_t total_blocks) {
    if (!bd || !path || block_size == 0 || total_blocks == 0) {
        return -1;
    }

    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }

    off_t size = (off_t)block_size * (off_t)total_blocks;
    if (ftruncate(fd, size) != 0) {
        close(fd);
        return -1;
    }

    bd->fd = fd;
    bd->block_size = block_size;
    return 0;
}

int bd_open(struct blockdev *bd, const char *path, uint32_t block_size) {
    if (!bd || !path || block_size == 0) {
        return -1;
    }
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        return -1;
    }
    bd->fd = fd;
    bd->block_size = block_size;
    return 0;
}

static ssize_t full_pread(int fd, void *buf, size_t count, off_t offset) {
    size_t done = 0;
    while (done < count) {
        ssize_t r = pread(fd, (char *)buf + done, count - done, offset + done);
        if (r <= 0) {
            return -1;
        }
        done += (size_t)r;
    }
    return (ssize_t)done;
}

static ssize_t full_pwrite(int fd, const void *buf, size_t count, off_t offset) {
    size_t done = 0;
    while (done < count) {
        ssize_t r = pwrite(fd, (const char *)buf + done, count - done, offset + done);
        if (r <= 0) {
            return -1;
        }
        done += (size_t)r;
    }
    return (ssize_t)done;
}

int bd_read(struct blockdev *bd, uint32_t block, void *buf) {
    if (!bd || bd->fd < 0 || !buf) {
        return -1;
    }
    off_t offset = (off_t)block * (off_t)bd->block_size;
    if (full_pread(bd->fd, buf, bd->block_size, offset) < 0) {
        return -1;
    }
    return 0;
}

int bd_write(struct blockdev *bd, uint32_t block, const void *buf) {
    if (!bd || bd->fd < 0 || !buf) {
        return -1;
    }
    off_t offset = (off_t)block * (off_t)bd->block_size;
    if (full_pwrite(bd->fd, buf, bd->block_size, offset) < 0) {
        return -1;
    }
    return 0;
}

void bd_close(struct blockdev *bd) {
    if (!bd) {
        return;
    }
    if (bd->fd >= 0) {
        close(bd->fd);
    }
    bd->fd = -1;
    bd->block_size = 0;
}
