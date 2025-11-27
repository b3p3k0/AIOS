#include "fs.h"
#include "blockdev.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t div_ceil(uint32_t x, uint32_t y) {
    return (x + y - 1u) / y;
}

static int bitmap_test(uint8_t *bm, uint32_t idx) {
    return (bm[idx / 8u] >> (idx % 8u)) & 1u;
}

static void bitmap_set(uint8_t *bm, uint32_t idx) {
    bm[idx / 8u] |= (uint8_t)(1u << (idx % 8u));
}

static void bitmap_clear(uint8_t *bm, uint32_t idx) {
    bm[idx / 8u] &= (uint8_t)~(1u << (idx % 8u));
}

static int sync_bitmap(struct fs *fs, uint8_t *bm, uint32_t start_block, uint32_t block_count) {
    uint32_t bs = fs->sb.block_size;
    for (uint32_t i = 0; i < block_count; ++i) {
        if (bd_write(&fs->bd, start_block + i, bm + i * bs) != 0) {
            return -1;
        }
    }
    return 0;
}

static int load_bitmap(struct fs *fs, uint8_t **out, uint32_t start_block, uint32_t block_count) {
    uint32_t bs = fs->sb.block_size;
    uint8_t *buf = calloc(block_count, bs);
    if (!buf) {
        return -1;
    }
    for (uint32_t i = 0; i < block_count; ++i) {
        if (bd_read(&fs->bd, start_block + i, buf + i * bs) != 0) {
            free(buf);
            return -1;
        }
    }
    *out = buf;
    return 0;
}

static int write_superblock(struct fs *fs) {
    uint32_t bs = fs->sb.block_size;
    uint8_t *buf = calloc(1, bs);
    if (!buf) {
        return -1;
    }
    memcpy(buf, &fs->sb, sizeof(fs->sb));
    int rc = bd_write(&fs->bd, 0, buf);
    free(buf);
    return rc;
}

static int layout_compute(struct fs_superblock *sb, uint32_t total_blocks, uint32_t inode_count, uint32_t block_size) {
    memset(sb, 0, sizeof(*sb));
    sb->magic = FS_MAGIC;
    sb->block_size = block_size;
    sb->total_blocks = total_blocks;
    sb->inode_count = inode_count;

    uint32_t bits_per_block = block_size * 8u;
    sb->inode_bitmap_start = 1;
    sb->inode_bitmap_blocks = div_ceil(inode_count, bits_per_block);
    if (sb->inode_bitmap_blocks == 0) {
        sb->inode_bitmap_blocks = 1; /* at least one block */
    }

    sb->inode_table_start = sb->inode_bitmap_start + sb->inode_bitmap_blocks;
    uint32_t inode_bytes = inode_count * sizeof(struct fs_inode);
    sb->inode_table_blocks = div_ceil(inode_bytes, block_size);

    sb->data_bitmap_start = sb->inode_table_start + sb->inode_table_blocks;
    /* Keep bitmap slightly over-provisioned so layout math stays simple. */
    sb->data_bitmap_blocks = div_ceil(total_blocks, bits_per_block);
    if (sb->data_bitmap_blocks == 0) {
        sb->data_bitmap_blocks = 1;
    }

    sb->data_region_start = sb->data_bitmap_start + sb->data_bitmap_blocks;
    if (sb->data_region_start >= total_blocks) {
        return -1;
    }
    sb->data_region_blocks = total_blocks - sb->data_region_start;
    sb->root_inode = 1; /* reserve inode 1 for root */
    return 0;
}

static int read_superblock(struct fs *fs) {
    uint32_t bs = FS_DEFAULT_BLOCK_SIZE;
    fs->bd.block_size = bs;
    uint8_t *buf = calloc(1, bs);
    if (!buf) {
        return -1;
    }
    if (bd_read(&fs->bd, 0, buf) != 0) {
        free(buf);
        return -1;
    }
    memcpy(&fs->sb, buf, sizeof(fs->sb));
    free(buf);
    if (fs->sb.magic != FS_MAGIC) {
        return -1;
    }
    if (fs->sb.block_size == 0 || fs->sb.block_size != bs) {
        /* This implementation only supports fixed block size. */
        return -1;
    }
    return 0;
}

static int read_inode(struct fs *fs, uint32_t ino, struct fs_inode *out) {
    if (ino == 0 || ino >= fs->sb.inode_count) {
        return -1;
    }
    uint32_t bs = fs->sb.block_size;
    uint32_t offset_bytes = ino * sizeof(struct fs_inode);
    uint32_t blk = fs->sb.inode_table_start + offset_bytes / bs;
    uint32_t within = offset_bytes % bs;

    uint8_t *buf = calloc(1, bs);
    if (!buf) {
        return -1;
    }
    if (bd_read(&fs->bd, blk, buf) != 0) {
        free(buf);
        return -1;
    }
    memcpy(out, buf + within, sizeof(struct fs_inode));
    free(buf);
    return 0;
}

static int write_inode(struct fs *fs, uint32_t ino, const struct fs_inode *in) {
    if (ino == 0 || ino >= fs->sb.inode_count) {
        return -1;
    }
    uint32_t bs = fs->sb.block_size;
    uint32_t offset_bytes = ino * sizeof(struct fs_inode);
    uint32_t blk = fs->sb.inode_table_start + offset_bytes / bs;
    uint32_t within = offset_bytes % bs;

    uint8_t *buf = calloc(1, bs);
    if (!buf) {
        return -1;
    }
    if (bd_read(&fs->bd, blk, buf) != 0) {
        free(buf);
        return -1;
    }
    memcpy(buf + within, in, sizeof(struct fs_inode));
    int rc = bd_write(&fs->bd, blk, buf);
    free(buf);
    return rc;
}

static int alloc_from_bitmap(uint8_t *bm, uint32_t start, uint32_t limit, uint32_t *out) {
    for (uint32_t i = start; i < limit; ++i) {
        if (!bitmap_test(bm, i)) {
            bitmap_set(bm, i);
            *out = i;
            return 0;
        }
    }
    return -1;
}

static int alloc_inode(struct fs *fs, uint32_t *out) {
    uint32_t ino;
    if (alloc_from_bitmap(fs->inode_bitmap, 1, fs->sb.inode_count, &ino) != 0) {
        return -1;
    }
    if (sync_bitmap(fs, fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks) != 0) {
        return -1;
    }
    *out = ino;
    return 0;
}

static int alloc_data_block(struct fs *fs, uint32_t *out_blockno) {
    uint32_t idx;
    if (alloc_from_bitmap(fs->data_bitmap, 0, fs->sb.data_region_blocks, &idx) != 0) {
        return -1;
    }
    if (sync_bitmap(fs, fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks) != 0) {
        return -1;
    }
    *out_blockno = fs->sb.data_region_start + idx;
    return 0;
}

static int free_inode_id(struct fs *fs, uint32_t ino) {
    if (ino == 0 || ino >= fs->sb.inode_count) {
        return -1;
    }
    bitmap_clear(fs->inode_bitmap, ino);
    return sync_bitmap(fs, fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks);
}

static int free_data_block(struct fs *fs, uint32_t abs_blockno) {
    if (abs_blockno < fs->sb.data_region_start ||
        abs_blockno >= fs->sb.data_region_start + fs->sb.data_region_blocks) {
        return -1;
    }
    uint32_t idx = abs_blockno - fs->sb.data_region_start;
    bitmap_clear(fs->data_bitmap, idx);
    return sync_bitmap(fs, fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks);
}

static int zero_block(struct fs *fs, uint32_t block) {
    uint8_t *buf = calloc(1, fs->sb.block_size);
    if (!buf) {
        return -1;
    }
    int rc = bd_write(&fs->bd, block, buf);
    free(buf);
    return rc;
}

static int ensure_capacity(struct fs *fs, struct fs_inode *node, uint32_t new_size) {
    uint32_t needed_blocks = div_ceil(new_size, fs->sb.block_size);
    if (needed_blocks > FS_DIRECT_BLOCKS) {
        return -1; /* exceeds max file size */
    }
    for (uint32_t i = 0; i < needed_blocks; ++i) {
        if (node->direct[i] == 0) {
            uint32_t blockno;
            if (alloc_data_block(fs, &blockno) != 0) {
                return -1;
            }
            node->direct[i] = blockno;
            if (zero_block(fs, blockno) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int read_data(struct fs *fs, struct fs_inode *node, uint32_t offset, uint8_t *out, size_t len, size_t *read_out) {
    if (offset >= node->size) {
        *read_out = 0;
        return 0;
    }
    size_t to_read = len;
    if (offset + to_read > node->size) {
        to_read = node->size - offset;
    }
    size_t done = 0;
    uint32_t bs = fs->sb.block_size;
    while (done < to_read) {
        uint32_t block_idx = (offset + done) / bs;
        uint32_t block_off = (offset + done) % bs;
        uint32_t abs_block = node->direct[block_idx];
        if (abs_block == 0) {
            return -1;
        }
        uint8_t *buf = calloc(1, bs);
        if (!buf) {
            return -1;
        }
        if (bd_read(&fs->bd, abs_block, buf) != 0) {
            free(buf);
            return -1;
        }
        size_t chunk = bs - block_off;
        if (chunk > to_read - done) {
            chunk = to_read - done;
        }
        memcpy(out + done, buf + block_off, chunk);
        done += chunk;
        free(buf);
    }
    *read_out = done;
    return 0;
}

static int write_data(struct fs *fs, struct fs_inode *node, uint32_t offset, const uint8_t *data, size_t len) {
    uint32_t new_end = offset + (uint32_t)len;
    if (new_end > node->size) {
        if (ensure_capacity(fs, node, new_end) != 0) {
            return -1;
        }
    }
    uint32_t bs = fs->sb.block_size;
    size_t done = 0;
    while (done < len) {
        uint32_t block_idx = (offset + done) / bs;
        uint32_t block_off = (offset + done) % bs;
        uint32_t abs_block = node->direct[block_idx];
        uint8_t *buf = calloc(1, bs);
        if (!buf) {
            return -1;
        }
        if (bd_read(&fs->bd, abs_block, buf) != 0) {
            free(buf);
            return -1;
        }
        size_t chunk = bs - block_off;
        if (chunk > len - done) {
            chunk = len - done;
        }
        memcpy(buf + block_off, data + done, chunk);
        if (bd_write(&fs->bd, abs_block, buf) != 0) {
            free(buf);
            return -1;
        }
        done += chunk;
        free(buf);
    }
    if (new_end > node->size) {
        node->size = new_end;
    }
    return 0;
}

static int dir_entry_count(struct fs_inode *node) {
    return (int)(node->size / sizeof(struct fs_dirent_disk));
}

static int dir_read_entry(struct fs *fs, struct fs_inode *dir, int index, struct fs_dirent_disk *out) {
    size_t read_bytes = 0;
    uint32_t offset = (uint32_t)(index * (int)sizeof(struct fs_dirent_disk));
    return read_data(fs, dir, offset, (uint8_t *)out, sizeof(struct fs_dirent_disk), &read_bytes);
}

static int dir_write_entry(struct fs *fs, struct fs_inode *dir, int index, const struct fs_dirent_disk *ent) {
    uint32_t offset = (uint32_t)(index * (int)sizeof(struct fs_dirent_disk));
    return write_data(fs, dir, offset, (const uint8_t *)ent, sizeof(struct fs_dirent_disk));
}

static int dir_append_entry(struct fs *fs, struct fs_inode *dir, const struct fs_dirent_disk *ent) {
    int count = dir_entry_count(dir);
    int slots = FS_DIRECT_BLOCKS * (int)(fs->sb.block_size / sizeof(struct fs_dirent_disk));
    for (int i = 0; i < count; ++i) {
        struct fs_dirent_disk tmp;
        if (dir_read_entry(fs, dir, i, &tmp) != 0) {
            return -1;
        }
        if (tmp.inode == 0) {
            return dir_write_entry(fs, dir, i, ent);
        }
    }
    if (count >= slots) {
        return -1; /* directory full */
    }
    int rc = dir_write_entry(fs, dir, count, ent);
    if (rc == 0) {
        dir->size += sizeof(struct fs_dirent_disk);
    }
    return rc;
}

static int dir_find_entry(struct fs *fs, struct fs_inode *dir, const char *name, struct fs_dirent_disk *out, int *index_out) {
    int count = dir_entry_count(dir);
    for (int i = 0; i < count; ++i) {
        struct fs_dirent_disk tmp;
        if (dir_read_entry(fs, dir, i, &tmp) != 0) {
            return -1;
        }
        if (tmp.inode != 0 && strncmp(tmp.name, name, FS_MAX_NAME) == 0) {
            if (out) {
                *out = tmp;
            }
            if (index_out) {
                *index_out = i;
            }
            return 0;
        }
    }
    return -1;
}

static int dir_is_empty(struct fs *fs, struct fs_inode *dir) {
    int count = dir_entry_count(dir);
    for (int i = 0; i < count; ++i) {
        struct fs_dirent_disk tmp;
        if (dir_read_entry(fs, dir, i, &tmp) != 0) {
            return -1;
        }
        if (tmp.inode != 0 && strcmp(tmp.name, ".") != 0 && strcmp(tmp.name, "..") != 0) {
            return 0;
        }
    }
    return 1;
}

static int parse_component(const char **p, char *out) {
    const char *s = *p;
    while (*s == '/') s++;
    if (*s == '\0') {
        return 0;
    }
    int len = 0;
    while (*s && *s != '/' && len < FS_MAX_NAME - 1) {
        out[len++] = *s++;
    }
    out[len] = '\0';
    while (*s == '/') s++;
    *p = s;
    return 1;
}

static int resolve_path(fs_t *fs, uint32_t start_inode, const char *path, uint32_t *out_inode, struct fs_inode *out_node) {
    uint32_t current = start_inode;
    struct fs_inode node;
    if (read_inode(fs, current, &node) != 0) {
        return -1;
    }

    if (path[0] == '/') {
        current = fs->sb.root_inode;
        if (read_inode(fs, current, &node) != 0) {
            return -1;
        }
    }

    const char *p = path;
    char comp[FS_MAX_NAME];
    while (parse_component(&p, comp)) {
        if (strcmp(comp, ".") == 0) {
            continue;
        }
        if (strcmp(comp, "..") == 0) {
            struct fs_dirent_disk parent;
            if (node.type != FS_INODE_DIR) {
                return -1;
            }
            if (dir_find_entry(fs, &node, "..", &parent, NULL) != 0) {
                return -1;
            }
            current = parent.inode;
            if (read_inode(fs, current, &node) != 0) {
                return -1;
            }
            continue;
        }
        if (node.type != FS_INODE_DIR) {
            return -1;
        }
        struct fs_dirent_disk ent;
        if (dir_find_entry(fs, &node, comp, &ent, NULL) != 0) {
            return -1;
        }
        current = ent.inode;
        if (read_inode(fs, current, &node) != 0) {
            return -1;
        }
    }
    if (out_node) {
        *out_node = node;
    }
    if (out_inode) {
        *out_inode = current;
    }
    return 0;
}

static int split_parent(const char *path, char *parent_out, char *leaf_out) {
    if (!path || !parent_out || !leaf_out) {
        return -1;
    }
    size_t len = strlen(path);
    if (len == 0) {
        return -1;
    }
    char tmp[FS_MAX_PATH];
    if (len >= sizeof(tmp)) {
        return -1;
    }
    strcpy(tmp, path);
    if (tmp[len - 1] == '/' && len > 1) {
        tmp[len - 1] = '\0';
    }
    char *slash = strrchr(tmp, '/');
    if (!slash) {
        strcpy(parent_out, ".");
        strncpy(leaf_out, tmp, FS_MAX_NAME - 1);
        leaf_out[FS_MAX_NAME - 1] = '\0';
        return 0;
    }
    *slash = '\0';
    strncpy(leaf_out, slash + 1, FS_MAX_NAME - 1);
    leaf_out[FS_MAX_NAME - 1] = '\0';
    if (slash == tmp) {
        strcpy(parent_out, "/");
    } else {
        strncpy(parent_out, tmp, FS_MAX_PATH - 1);
        parent_out[FS_MAX_PATH - 1] = '\0';
    }
    return 0;
}

int fs_format(fs_t *fs, const char *image_path, uint32_t total_blocks, uint32_t inode_count, uint32_t block_size) {
    if (block_size == 0) {
        block_size = FS_DEFAULT_BLOCK_SIZE;
    }
    if (!fs || !image_path) {
        return -1;
    }
    memset(fs, 0, sizeof(*fs));
    fs->bd.fd = -1;

    if (layout_compute(&fs->sb, total_blocks, inode_count, block_size) != 0) {
        return -1;
    }
    if (bd_create(&fs->bd, image_path, block_size, total_blocks) != 0) {
        return -1;
    }

    size_t inode_bm_bytes = fs->sb.inode_bitmap_blocks * block_size;
    size_t data_bm_bytes = fs->sb.data_bitmap_blocks * block_size;
    fs->inode_bitmap = calloc(1, inode_bm_bytes);
    fs->data_bitmap = calloc(1, data_bm_bytes);
    if (!fs->inode_bitmap || !fs->data_bitmap) {
        return -1;
    }

    /* Reserve root inode. */
    bitmap_set(fs->inode_bitmap, fs->sb.root_inode);
    if (sync_bitmap(fs, fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks) != 0) {
        return -1;
    }

    if (sync_bitmap(fs, fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks) != 0) {
        return -1;
    }

    /* Zero inode table region. */
    uint8_t *zero = calloc(1, block_size);
    if (!zero) {
        return -1;
    }
    for (uint32_t b = 0; b < fs->sb.inode_table_blocks; ++b) {
        if (bd_write(&fs->bd, fs->sb.inode_table_start + b, zero) != 0) {
            free(zero);
            return -1;
        }
    }
    free(zero);

    /* Initialize root directory inode. */
    struct fs_inode root;
    memset(&root, 0, sizeof(root));
    root.type = FS_INODE_DIR;
    root.size = 0;

    uint32_t root_block;
    if (alloc_data_block(fs, &root_block) != 0) {
        return -1;
    }
    root.direct[0] = root_block;
    if (write_inode(fs, fs->sb.root_inode, &root) != 0) {
        return -1;
    }

    /* Add . and .. */
    struct fs_dirent_disk dot = { .inode = fs->sb.root_inode, .type = FS_INODE_DIR, .name = "." };
    struct fs_dirent_disk dotdot = { .inode = fs->sb.root_inode, .type = FS_INODE_DIR, .name = ".." };
    if (dir_append_entry(fs, &root, &dot) != 0) {
        return -1;
    }
    if (dir_append_entry(fs, &root, &dotdot) != 0) {
        return -1;
    }
    if (write_inode(fs, fs->sb.root_inode, &root) != 0) {
        return -1;
    }

    if (write_superblock(fs) != 0) {
        return -1;
    }
    return 0;
}

int fs_mount(fs_t *fs, const char *image_path) {
    if (!fs || !image_path) {
        return -1;
    }
    memset(fs, 0, sizeof(*fs));
    fs->bd.fd = -1;
    if (bd_open(&fs->bd, image_path, FS_DEFAULT_BLOCK_SIZE) != 0) {
        return -1;
    }
    if (read_superblock(fs) != 0) {
        bd_close(&fs->bd);
        return -1;
    }
    if (load_bitmap(fs, &fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks) != 0) {
        bd_close(&fs->bd);
        return -1;
    }
    if (load_bitmap(fs, &fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks) != 0) {
        free(fs->inode_bitmap);
        bd_close(&fs->bd);
        return -1;
    }
    return 0;
}

int fs_unmount(fs_t *fs) {
    if (!fs) {
        return -1;
    }
    if (fs->inode_bitmap) {
        sync_bitmap(fs, fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks);
        free(fs->inode_bitmap);
        fs->inode_bitmap = NULL;
    }
    if (fs->data_bitmap) {
        sync_bitmap(fs, fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks);
        free(fs->data_bitmap);
        fs->data_bitmap = NULL;
    }
    bd_close(&fs->bd);
    memset(fs, 0, sizeof(*fs));
    return 0;
}

uint32_t fs_root_inode(const fs_t *fs) {
    return fs->sb.root_inode;
}

int fs_lookup(fs_t *fs, uint32_t cwd_inode, const char *path, struct fs_inode *out_inode, uint32_t *out_ino) {
    return resolve_path(fs, cwd_inode, path, out_ino, out_inode);
}

int fs_make_dir(fs_t *fs, uint32_t cwd_inode, const char *path) {
    char parent_path[FS_MAX_PATH];
    char leaf[FS_MAX_NAME];
    if (split_parent(path, parent_path, leaf) != 0) {
        return -1;
    }
    uint32_t parent_ino;
    struct fs_inode parent;
    if (resolve_path(fs, cwd_inode, parent_path, &parent_ino, &parent) != 0) {
        return -1;
    }
    if (parent.type != FS_INODE_DIR) {
        return -1;
    }
    if (dir_find_entry(fs, &parent, leaf, NULL, NULL) == 0) {
        return -1; /* already exists */
    }

    uint32_t new_ino;
    if (alloc_inode(fs, &new_ino) != 0) {
        return -1;
    }
    struct fs_inode dir;
    memset(&dir, 0, sizeof(dir));
    dir.type = FS_INODE_DIR;
    dir.size = 0;
    uint32_t data_block;
    if (alloc_data_block(fs, &data_block) != 0) {
        return -1;
    }
    dir.direct[0] = data_block;

    struct fs_dirent_disk dot = { .inode = new_ino, .type = FS_INODE_DIR };
    snprintf(dot.name, FS_MAX_NAME, "%s", ".");
    struct fs_dirent_disk dotdot = { .inode = parent_ino, .type = FS_INODE_DIR };
    snprintf(dotdot.name, FS_MAX_NAME, "%s", "..");

    if (dir_append_entry(fs, &dir, &dot) != 0) {
        return -1;
    }
    if (dir_append_entry(fs, &dir, &dotdot) != 0) {
        return -1;
    }
    if (write_inode(fs, new_ino, &dir) != 0) {
        return -1;
    }

    struct fs_dirent_disk ent = { .inode = new_ino, .type = FS_INODE_DIR };
    snprintf(ent.name, FS_MAX_NAME, "%s", leaf);
    if (dir_append_entry(fs, &parent, &ent) != 0) {
        return -1;
    }
    if (write_inode(fs, parent_ino, &parent) != 0) {
        return -1;
    }
    return 0;
}

int fs_create_file(fs_t *fs, uint32_t cwd_inode, const char *path) {
    char parent_path[FS_MAX_PATH];
    char leaf[FS_MAX_NAME];
    if (split_parent(path, parent_path, leaf) != 0) {
        return -1;
    }
    uint32_t parent_ino;
    struct fs_inode parent;
    if (resolve_path(fs, cwd_inode, parent_path, &parent_ino, &parent) != 0) {
        return -1;
    }
    if (parent.type != FS_INODE_DIR) {
        return -1;
    }
    if (dir_find_entry(fs, &parent, leaf, NULL, NULL) == 0) {
        return -1;
    }

    uint32_t ino;
    if (alloc_inode(fs, &ino) != 0) {
        return -1;
    }
    struct fs_inode file;
    memset(&file, 0, sizeof(file));
    file.type = FS_INODE_FILE;
    file.size = 0;
    if (write_inode(fs, ino, &file) != 0) {
        return -1;
    }

    struct fs_dirent_disk ent = { .inode = ino, .type = FS_INODE_FILE };
    snprintf(ent.name, FS_MAX_NAME, "%s", leaf);
    if (dir_append_entry(fs, &parent, &ent) != 0) {
        return -1;
    }
    if (write_inode(fs, parent_ino, &parent) != 0) {
        return -1;
    }
    return 0;
}

int fs_write_file(fs_t *fs, uint32_t cwd_inode, const char *path, const uint8_t *data, size_t len, uint32_t offset) {
    uint32_t ino;
    struct fs_inode file;
    if (resolve_path(fs, cwd_inode, path, &ino, &file) != 0) {
        return -1;
    }
    if (file.type != FS_INODE_FILE) {
        return -1;
    }
    if (write_data(fs, &file, offset, data, len) != 0) {
        return -1;
    }
    if (write_inode(fs, ino, &file) != 0) {
        return -1;
    }
    return 0;
}

int fs_read_file(fs_t *fs, uint32_t cwd_inode, const char *path, uint8_t *out, size_t len, uint32_t offset, size_t *bytes_read) {
    uint32_t ino;
    struct fs_inode file;
    if (resolve_path(fs, cwd_inode, path, &ino, &file) != 0) {
        return -1;
    }
    if (file.type != FS_INODE_FILE) {
        return -1;
    }
    return read_data(fs, &file, offset, out, len, bytes_read);
}

int fs_list_dir(fs_t *fs, uint32_t cwd_inode, const char *path, struct fs_dirent_disk **out_entries, size_t *out_count) {
    uint32_t ino;
    struct fs_inode dir;
    if (resolve_path(fs, cwd_inode, path, &ino, &dir) != 0) {
        return -1;
    }
    if (dir.type != FS_INODE_DIR) {
        return -1;
    }
    int count = dir_entry_count(&dir);
    if (count < 0) {
        return -1;
    }
    struct fs_dirent_disk *list = calloc((size_t)count, sizeof(struct fs_dirent_disk));
    if (!list) {
        return -1;
    }
    int out_idx = 0;
    for (int i = 0; i < count; ++i) {
        struct fs_dirent_disk ent;
        if (dir_read_entry(fs, &dir, i, &ent) != 0) {
            free(list);
            return -1;
        }
        if (ent.inode != 0) {
            list[out_idx++] = ent;
        }
    }
    *out_entries = list;
    *out_count = (size_t)out_idx;
    return 0;
}

int fs_delete(fs_t *fs, uint32_t cwd_inode, const char *path) {
    char parent_path[FS_MAX_PATH];
    char leaf[FS_MAX_NAME];
    if (split_parent(path, parent_path, leaf) != 0) {
        return -1;
    }
    uint32_t parent_ino;
    struct fs_inode parent;
    if (resolve_path(fs, cwd_inode, parent_path, &parent_ino, &parent) != 0) {
        return -1;
    }
    if (parent.type != FS_INODE_DIR) {
        return -1;
    }
    int entry_index = -1;
    struct fs_dirent_disk ent;
    if (dir_find_entry(fs, &parent, leaf, &ent, &entry_index) != 0) {
        return -1;
    }

    struct fs_inode victim;
    if (read_inode(fs, ent.inode, &victim) != 0) {
        return -1;
    }
    if (victim.type == FS_INODE_DIR) {
        int empty = dir_is_empty(fs, &victim);
        if (empty != 1) {
            return -1; /* not empty or error */
        }
    }

    /* Free data blocks. */
    uint32_t bs = fs->sb.block_size;
    uint32_t blocks_used = div_ceil(victim.size, bs);
    for (uint32_t i = 0; i < blocks_used; ++i) {
        if (victim.direct[i] != 0) {
            if (free_data_block(fs, victim.direct[i]) != 0) {
                return -1;
            }
        }
    }

    if (free_inode_id(fs, ent.inode) != 0) {
        return -1;
    }

    /* Clear dirent slot. */
    struct fs_dirent_disk empty_ent;
    memset(&empty_ent, 0, sizeof(empty_ent));
    if (dir_write_entry(fs, &parent, entry_index, &empty_ent) != 0) {
        return -1;
    }
    if (write_inode(fs, parent_ino, &parent) != 0) {
        return -1;
    }
    return 0;
}
