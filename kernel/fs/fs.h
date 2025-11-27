#ifndef AIOS_FS_H
#define AIOS_FS_H

#include <stdint.h>
#include <stddef.h>
#include "blockdev.h"

#define FS_MAGIC 0x41494f53u /* "AIOS" */
#define FS_DEFAULT_BLOCK_SIZE 4096u
#define FS_DIRECT_BLOCKS 8
#define FS_MAX_NAME 32
#define FS_MAX_PATH 512

enum fs_inode_type {
    FS_INODE_FREE = 0,
    FS_INODE_FILE = 1,
    FS_INODE_DIR = 2,
};

struct fs_superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t inode_bitmap_start;
    uint32_t inode_bitmap_blocks;
    uint32_t data_bitmap_start;
    uint32_t data_bitmap_blocks;
    uint32_t inode_table_start;
    uint32_t inode_table_blocks;
    uint32_t data_region_start;
    uint32_t data_region_blocks;
    uint32_t root_inode;
};

struct fs_inode {
    uint8_t type; /* fs_inode_type */
    uint8_t reserved[3];
    uint32_t size; /* bytes for files; dirent bytes for dirs */
    uint32_t direct[FS_DIRECT_BLOCKS]; /* absolute block numbers */
};

struct fs_dirent_disk {
    uint32_t inode; /* 0 == unused slot */
    uint8_t type;   /* fs_inode_type */
    char name[FS_MAX_NAME];
};

typedef struct fs {
    struct blockdev bd;
    struct fs_superblock sb;
    uint8_t *inode_bitmap;
    uint8_t *data_bitmap;
} fs_t;

int fs_format(fs_t *fs, struct blockdev *bd, uint32_t inode_count);
int fs_mount(fs_t *fs, struct blockdev *bd);

uint32_t fs_root_inode(const fs_t *fs);

int fs_lookup(fs_t *fs, uint32_t cwd_inode, const char *path, struct fs_inode *out_inode, uint32_t *out_ino);
int fs_make_dir(fs_t *fs, uint32_t cwd_inode, const char *path);
int fs_create_file(fs_t *fs, uint32_t cwd_inode, const char *path);
int fs_delete(fs_t *fs, uint32_t cwd_inode, const char *path);
int fs_write_file(fs_t *fs, uint32_t cwd_inode, const char *path, const uint8_t *data, size_t len, uint32_t offset);
int fs_read_file(fs_t *fs, uint32_t cwd_inode, const char *path, uint8_t *out, size_t len, uint32_t offset, size_t *bytes_read);
int fs_list_dir(fs_t *fs, uint32_t cwd_inode, const char *path, struct fs_dirent_disk **out_entries, size_t *out_count);

#endif /* AIOS_FS_H */
