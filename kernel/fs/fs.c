#include "fs.h"
#include "blockdev.h"
#include "mem.h"
#include "util.h"

static uint32_t div_ceil(uint32_t x, uint32_t y) { return (x + y - 1u) / y; }

static int bitmap_test(uint8_t *bm, uint32_t idx) { return (bm[idx / 8u] >> (idx % 8u)) & 1u; }
static void bitmap_set(uint8_t *bm, uint32_t idx) { bm[idx / 8u] |= (uint8_t)(1u << (idx % 8u)); }
static void bitmap_clear(uint8_t *bm, uint32_t idx) { bm[idx / 8u] &= (uint8_t)~(1u << (idx % 8u)); }

static int sync_bitmap(struct fs *fs, uint8_t *bm, uint32_t start_block, uint32_t block_count) {
    uint32_t bs = fs->sb.block_size;
    for (uint32_t i = 0; i < block_count; ++i) {
        if (bd_write(&fs->bd, start_block + i, bm + i * bs) != 0) return -1;
    }
    return 0;
}

static int load_bitmap(struct fs *fs, uint8_t **out, uint32_t start_block, uint32_t block_count) {
    uint32_t bs = fs->sb.block_size;
    uint8_t *buf = kcalloc(block_count, bs);
    if (!buf) return -1;
    for (uint32_t i = 0; i < block_count; ++i) {
        if (bd_read(&fs->bd, start_block + i, buf + i * bs) != 0) return -1;
    }
    *out = buf;
    return 0;
}

static int write_superblock(struct fs *fs) {
    uint32_t bs = fs->sb.block_size;
    uint8_t *buf = kcalloc(1, bs);
    if (!buf) return -1;
    memcpy(buf, &fs->sb, sizeof(fs->sb));
    int rc = bd_write(&fs->bd, 0, buf);
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
    if (sb->inode_bitmap_blocks == 0) sb->inode_bitmap_blocks = 1;

    sb->inode_table_start = sb->inode_bitmap_start + sb->inode_bitmap_blocks;
    uint32_t inode_bytes = inode_count * sizeof(struct fs_inode);
    sb->inode_table_blocks = div_ceil(inode_bytes, block_size);

    sb->data_bitmap_start = sb->inode_table_start + sb->inode_table_blocks;
    sb->data_bitmap_blocks = div_ceil(total_blocks, bits_per_block);
    if (sb->data_bitmap_blocks == 0) sb->data_bitmap_blocks = 1;

    sb->data_region_start = sb->data_bitmap_start + sb->data_bitmap_blocks;
    if (sb->data_region_start >= total_blocks) return -1;
    sb->data_region_blocks = total_blocks - sb->data_region_start;
    sb->root_inode = 1;
    return 0;
}

static int read_superblock(struct fs *fs) {
    uint32_t bs = fs->bd.block_size;
    uint8_t *buf = kcalloc(1, bs);
    if (!buf) return -1;
    if (bd_read(&fs->bd, 0, buf) != 0) return -1;
    memcpy(&fs->sb, buf, sizeof(fs->sb));
    if (fs->sb.magic != FS_MAGIC) return -1;
    if (fs->sb.block_size != bs) return -1;
    return 0;
}

static int read_inode(struct fs *fs, uint32_t ino, struct fs_inode *out) {
    if (ino == 0 || ino >= fs->sb.inode_count) return -1;
    uint32_t bs = fs->sb.block_size;
    uint32_t off = ino * sizeof(struct fs_inode);
    uint32_t blk = fs->sb.inode_table_start + off / bs;
    uint32_t within = off % bs;
    uint8_t *buf = kcalloc(1, bs);
    if (!buf) return -1;
    if (bd_read(&fs->bd, blk, buf) != 0) return -1;
    memcpy(out, buf + within, sizeof(*out));
    return 0;
}

static int write_inode(struct fs *fs, uint32_t ino, const struct fs_inode *in) {
    if (ino == 0 || ino >= fs->sb.inode_count) return -1;
    uint32_t bs = fs->sb.block_size;
    uint32_t off = ino * sizeof(struct fs_inode);
    uint32_t blk = fs->sb.inode_table_start + off / bs;
    uint32_t within = off % bs;
    uint8_t *buf = kcalloc(1, bs);
    if (!buf) return -1;
    if (bd_read(&fs->bd, blk, buf) != 0) return -1;
    memcpy(buf + within, in, sizeof(*in));
    return bd_write(&fs->bd, blk, buf);
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
    if (alloc_from_bitmap(fs->inode_bitmap, 1, fs->sb.inode_count, out) != 0) return -1;
    if (sync_bitmap(fs, fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks) != 0) return -1;
    return 0;
}

static int alloc_data_block(struct fs *fs, uint32_t *out_blockno) {
    uint32_t idx;
    if (alloc_from_bitmap(fs->data_bitmap, 0, fs->sb.data_region_blocks, &idx) != 0) return -1;
    if (sync_bitmap(fs, fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks) != 0) return -1;
    *out_blockno = fs->sb.data_region_start + idx;
    return 0;
}

static int free_inode_id(struct fs *fs, uint32_t ino) {
    if (ino == 0 || ino >= fs->sb.inode_count) return -1;
    bitmap_clear(fs->inode_bitmap, ino);
    return sync_bitmap(fs, fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks);
}

static int free_data_block_id(struct fs *fs, uint32_t abs_block) {
    if (abs_block < fs->sb.data_region_start || abs_block >= fs->sb.total_blocks) return -1;
    uint32_t idx = abs_block - fs->sb.data_region_start;
    bitmap_clear(fs->data_bitmap, idx);
    return sync_bitmap(fs, fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks);
}

static int dir_load(fs_t *fs, const struct fs_inode *dir, uint8_t **out) {
    uint32_t bs = fs->sb.block_size;
    uint8_t *buf = kcalloc(1, bs);
    if (!buf) return -1;
    if (dir->direct[0] == 0) return -1;
    if (bd_read(&fs->bd, dir->direct[0], buf) != 0) return -1;
    *out = buf;
    return 0;
}

static int dir_save(fs_t *fs, struct fs_inode *dir, uint8_t *buf) {
    if (dir->direct[0] == 0) return -1;
    return bd_write(&fs->bd, dir->direct[0], buf);
}

static int dir_find_entry(fs_t *fs, struct fs_inode *dir, const char *name, struct fs_dirent_disk *out_ent, uint32_t *out_index) {
    uint8_t *buf = NULL;
    if (dir_load(fs, dir, &buf) != 0) return -1;
    uint32_t count = dir->size / sizeof(struct fs_dirent_disk);
    struct fs_dirent_disk *ents = (struct fs_dirent_disk *)buf;
    for (uint32_t i = 0; i < count; ++i) {
        if (ents[i].inode != 0 && strcmp(ents[i].name, name) == 0) {
            if (out_ent) memcpy(out_ent, &ents[i], sizeof(*out_ent));
            if (out_index) *out_index = i;
            return 0;
        }
    }
    return -1;
}

static int dir_add_entry(fs_t *fs, struct fs_inode *dir, uint32_t dir_ino, const char *name, uint32_t ino, uint8_t type) {
    uint32_t bs = fs->sb.block_size;
    uint8_t *buf = NULL;
    if (dir_load(fs, dir, &buf) != 0) return -1;
    uint32_t max_entries = bs / sizeof(struct fs_dirent_disk);
    struct fs_dirent_disk *ents = (struct fs_dirent_disk *)buf;
    uint32_t count = dir->size / sizeof(struct fs_dirent_disk);
    uint32_t target = max_entries;
    for (uint32_t i = 0; i < count; ++i) {
        if (ents[i].inode == 0) { target = i; break; }
    }
    if (target == max_entries) {
        if (count >= max_entries) return -1;
        target = count;
    }
    ents[target].inode = ino;
    ents[target].type = type;
    strncpy(ents[target].name, name, FS_MAX_NAME - 1);
    ents[target].name[FS_MAX_NAME - 1] = '\0';
    if (target == count) dir->size += sizeof(struct fs_dirent_disk);
    if (dir_save(fs, dir, buf) != 0) return -1;
    if (write_inode(fs, dir_ino, dir) != 0) return -1;
    return 0;
}

static int path_split_component(const char **p, char *component) {
    const char *s = *p;
    while (*s == '/') s++;
    if (*s == '\0') return 0;
    char *out = component;
    while (*s && *s != '/' && (out - component) < (FS_MAX_NAME - 1)) {
        *out++ = *s++;
    }
    *out = '\0';
    *p = s;
    return 1;
}

static int resolve_path(fs_t *fs, uint32_t start_ino, const char *path, struct fs_inode *out_inode, uint32_t *out_ino) {
    uint32_t cur_ino = (*path == '/') ? fs->sb.root_inode : start_ino;
    struct fs_inode cur;
    if (read_inode(fs, cur_ino, &cur) != 0) return -1;

    const char *p = path;
    char comp[FS_MAX_NAME];
    while (path_split_component(&p, comp)) {
        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            /* For simplicity, root's .. is itself */
            continue;
        }
        if (cur.type != FS_INODE_DIR) return -1;
        struct fs_dirent_disk ent;
        if (dir_find_entry(fs, &cur, comp, &ent, NULL) != 0) return -1;
        cur_ino = ent.inode;
        if (read_inode(fs, cur_ino, &cur) != 0) return -1;
    }
    if (out_inode) memcpy(out_inode, &cur, sizeof(cur));
    if (out_ino) *out_ino = cur_ino;
    return 0;
}

/* Public API */

int fs_format_ram(fs_t *fs, void *base, uint32_t bytes, uint32_t inode_count, uint32_t block_size) {
    if (bd_init_ram(&fs->bd, base, bytes, block_size) != 0) return -1;
    uint32_t total_blocks = bytes / block_size;
    if (layout_compute(&fs->sb, total_blocks, inode_count, block_size) != 0) return -1;

    /* Zero disk */
    uint8_t *zero = kcalloc(1, block_size);
    if (!zero) return -1;
    for (uint32_t b = 0; b < total_blocks; ++b) {
        bd_write(&fs->bd, b, zero);
    }

    /* Allocate bitmaps */
    fs->inode_bitmap = kcalloc(fs->sb.inode_bitmap_blocks, block_size);
    fs->data_bitmap  = kcalloc(fs->sb.data_bitmap_blocks, block_size);
    if (!fs->inode_bitmap || !fs->data_bitmap) return -1;

    /* Mark metadata blocks as used in data bitmap */
    for (uint32_t b = 0; b < fs->sb.data_region_start; ++b) {
        bitmap_set(fs->data_bitmap, b - fs->sb.data_region_start);
    }

    /* Reserve root inode */
    bitmap_set(fs->inode_bitmap, fs->sb.root_inode);
    if (sync_bitmap(fs, fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks) != 0) return -1;
    if (sync_bitmap(fs, fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks) != 0) return -1;

    if (write_superblock(fs) != 0) return -1;

    /* Initialize root inode */
    struct fs_inode root;
    memset(&root, 0, sizeof(root));
    root.type = FS_INODE_DIR;
    root.size = 0;
    if (alloc_data_block(fs, &root.direct[0]) != 0) return -1;

    /* Directory entries: . and .. */
    uint8_t *buf = kcalloc(1, block_size);
    struct fs_dirent_disk *ents = (struct fs_dirent_disk *)buf;
    ents[0].inode = fs->sb.root_inode;
    ents[0].type = FS_INODE_DIR;
    strcpy(ents[0].name, ".");
    ents[1].inode = fs->sb.root_inode;
    ents[1].type = FS_INODE_DIR;
    strcpy(ents[1].name, "..");
    root.size = 2 * sizeof(struct fs_dirent_disk);
    if (bd_write(&fs->bd, root.direct[0], buf) != 0) return -1;
    if (write_inode(fs, fs->sb.root_inode, &root) != 0) return -1;
    return 0;
}

int fs_mount_ram(fs_t *fs, void *base, uint32_t bytes) {
    if (bd_init_ram(&fs->bd, base, bytes, FS_DEFAULT_BLOCK_SIZE) != 0) return -1;
    if (read_superblock(fs) != 0) return -1;
    if (load_bitmap(fs, &fs->inode_bitmap, fs->sb.inode_bitmap_start, fs->sb.inode_bitmap_blocks) != 0) return -1;
    if (load_bitmap(fs, &fs->data_bitmap, fs->sb.data_bitmap_start, fs->sb.data_bitmap_blocks) != 0) return -1;
    return 0;
}

uint32_t fs_root_inode(const fs_t *fs) { return fs->sb.root_inode; }

int fs_lookup(fs_t *fs, uint32_t cwd_inode, const char *path, struct fs_inode *out_inode, uint32_t *out_ino) {
    return resolve_path(fs, cwd_inode, path, out_inode, out_ino);
}

int fs_make_dir(fs_t *fs, uint32_t cwd_inode, const char *path) {
    /* Split parent path and leaf */
    const char *last_slash = path;
    const char *p = path;
    while (*p) { if (*p == '/') last_slash = p; p++; }
    char leaf[FS_MAX_NAME];
    if (last_slash != path) {
        strncpy(leaf, last_slash + 1, FS_MAX_NAME-1);
        leaf[FS_MAX_NAME-1]=0;
    } else {
        strncpy(leaf, path, FS_MAX_NAME-1); leaf[FS_MAX_NAME-1]=0;
    }
    char parent_path[FS_MAX_PATH];
    size_t parent_len = (size_t)(last_slash - path);
    if (*last_slash == '/' && parent_len == 0) parent_len = 1; /* root */
    if (parent_len >= FS_MAX_PATH) return -1;
    strncpy(parent_path, path, parent_len);
    parent_path[parent_len] = '\0';
    if (parent_len == 0) strcpy(parent_path, ".");

    struct fs_inode parent;
    uint32_t parent_ino;
    if (resolve_path(fs, cwd_inode, parent_path, &parent, &parent_ino) != 0) return -1;
    if (parent.type != FS_INODE_DIR) return -1;

    /* allocate inode */
    uint32_t new_ino;
    if (alloc_inode(fs, &new_ino) != 0) return -1;
    struct fs_inode dir;
    memset(&dir, 0, sizeof(dir));
    dir.type = FS_INODE_DIR;
    if (alloc_data_block(fs, &dir.direct[0]) != 0) return -1;

    /* init dirents */
    uint8_t *buf = kcalloc(1, fs->sb.block_size);
    struct fs_dirent_disk *ents = (struct fs_dirent_disk *)buf;
    ents[0].inode = new_ino; ents[0].type = FS_INODE_DIR; strcpy(ents[0].name, ".");
    ents[1].inode = parent_ino; ents[1].type = FS_INODE_DIR; strcpy(ents[1].name, "..");
    dir.size = 2 * sizeof(struct fs_dirent_disk);
    bd_write(&fs->bd, dir.direct[0], buf);
    write_inode(fs, new_ino, &dir);

    return dir_add_entry(fs, &parent, parent_ino, leaf, new_ino, FS_INODE_DIR);
}

int fs_create_file(fs_t *fs, uint32_t cwd_inode, const char *path) {
    const char *last_slash = path;
    const char *p = path;
    while (*p) { if (*p == '/') last_slash = p; p++; }
    char leaf[FS_MAX_NAME];
    if (last_slash != path) { strncpy(leaf, last_slash + 1, FS_MAX_NAME-1); leaf[FS_MAX_NAME-1]=0; }
    else { strncpy(leaf, path, FS_MAX_NAME-1); leaf[FS_MAX_NAME-1]=0; }
    char parent_path[FS_MAX_PATH];
    size_t parent_len = (size_t)(last_slash - path);
    if (*last_slash == '/' && parent_len == 0) parent_len = 1;
    if (parent_len >= FS_MAX_PATH) return -1;
    strncpy(parent_path, path, parent_len);
    parent_path[parent_len] = '\0';
    if (parent_len == 0) strcpy(parent_path, ".");

    struct fs_inode parent;
    uint32_t parent_ino;
    if (resolve_path(fs, cwd_inode, parent_path, &parent, &parent_ino) != 0) return -1;
    if (parent.type != FS_INODE_DIR) return -1;

    uint32_t ino;
    if (alloc_inode(fs, &ino) != 0) return -1;
    struct fs_inode file;
    memset(&file, 0, sizeof(file));
    file.type = FS_INODE_FILE;
    file.size = 0;
    if (write_inode(fs, ino, &file) != 0) return -1;
    return dir_add_entry(fs, &parent, parent_ino, leaf, ino, FS_INODE_FILE);
}

int fs_delete(fs_t *fs, uint32_t cwd_inode, const char *path) {
    struct fs_inode target, parent;
    uint32_t target_ino, parent_ino;

    /* find parent and leaf */
    const char *last = path;
    const char *p = path;
    while (*p) { if (*p == '/') last = p; p++; }
    char leaf[FS_MAX_NAME];
    if (last != path) { strncpy(leaf, last+1, FS_MAX_NAME-1); leaf[FS_MAX_NAME-1]=0; }
    else { strncpy(leaf, path, FS_MAX_NAME-1); leaf[FS_MAX_NAME-1]=0; }
    char parent_path[FS_MAX_PATH];
    size_t parent_len = (size_t)(last - path);
    if (*last == '/' && parent_len == 0) parent_len = 1;
    if (parent_len >= FS_MAX_PATH) return -1;
    strncpy(parent_path, path, parent_len);
    parent_path[parent_len] = '\0';
    if (parent_len == 0) strcpy(parent_path, ".");

    if (resolve_path(fs, cwd_inode, parent_path, &parent, &parent_ino) != 0) return -1;
    if (parent.type != FS_INODE_DIR) return -1;
    if (dir_find_entry(fs, &parent, leaf, NULL, &target_ino) != 0) return -1;
    if (read_inode(fs, target_ino, &target) != 0) return -1;

    /* if dir, ensure empty (only . and ..) */
    if (target.type == FS_INODE_DIR) {
        uint8_t *buf = NULL;
        if (dir_load(fs, &target, &buf) != 0) return -1;
        uint32_t count = target.size / sizeof(struct fs_dirent_disk);
        struct fs_dirent_disk *ents = (struct fs_dirent_disk *)buf;
        for (uint32_t i = 0; i < count; ++i) {
            if (ents[i].inode != 0 && strcmp(ents[i].name, ".") != 0 && strcmp(ents[i].name, "..") != 0) {
                return -1;
            }
        }
    }

    /* free data blocks */
    for (int i = 0; i < FS_DIRECT_BLOCKS; ++i) {
        if (target.direct[i]) free_data_block_id(fs, target.direct[i]);
    }
    /* remove dirent */
    uint8_t *buf = NULL;
    if (dir_load(fs, &parent, &buf) != 0) return -1;
    uint32_t count = parent.size / sizeof(struct fs_dirent_disk);
    struct fs_dirent_disk *ents = (struct fs_dirent_disk *)buf;
    for (uint32_t i = 0; i < count; ++i) {
        if (ents[i].inode == target_ino) {
            ents[i].inode = 0;
            ents[i].name[0] = '\0';
            ents[i].type = FS_INODE_FREE;
            break;
        }
    }
    dir_save(fs, &parent, buf);
    write_inode(fs, parent_ino, &parent);
    free_inode_id(fs, target_ino);
    return 0;
}

int fs_write_file(fs_t *fs, uint32_t cwd_inode, const char *path, const uint8_t *data, size_t len, uint32_t offset) {
    struct fs_inode file;
    uint32_t ino;
    if (resolve_path(fs, cwd_inode, path, &file, &ino) != 0) return -1;
    if (file.type != FS_INODE_FILE) return -1;
    uint32_t bs = fs->sb.block_size;
    uint32_t max_bytes = FS_DIRECT_BLOCKS * bs;
    if (offset + len > max_bytes) return -1;

    uint8_t *buf = kcalloc(1, bs);
    if (!buf) return -1;
    size_t remaining = len;
    size_t written = 0;
    uint32_t pos = offset;
    while (remaining > 0) {
        uint32_t block_idx = pos / bs;
        uint32_t within = pos % bs;
        if (file.direct[block_idx] == 0) {
            if (alloc_data_block(fs, &file.direct[block_idx]) != 0) return -1;
        }
        bd_read(&fs->bd, file.direct[block_idx], buf);
        uint32_t chunk = (uint32_t)((remaining < (bs - within)) ? remaining : (bs - within));
        memcpy(buf + within, data + written, chunk);
        bd_write(&fs->bd, file.direct[block_idx], buf);
        remaining -= chunk;
        written += chunk;
        pos += chunk;
    }
    if (offset + len > file.size) file.size = offset + len;
    return write_inode(fs, ino, &file);
}

int fs_read_file(fs_t *fs, uint32_t cwd_inode, const char *path, uint8_t *out, size_t len, uint32_t offset, size_t *bytes_read) {
    struct fs_inode file;
    uint32_t ino;
    if (resolve_path(fs, cwd_inode, path, &file, &ino) != 0) return -1;
    if (file.type != FS_INODE_FILE) return -1;
    if (offset >= file.size) { *bytes_read = 0; return 0; }
    uint32_t bs = fs->sb.block_size;
    uint8_t *buf = kcalloc(1, bs);
    if (!buf) return -1;
    size_t remaining = (offset + len > file.size) ? (file.size - offset) : len;
    size_t read = 0;
    uint32_t pos = offset;
    while (remaining > 0) {
        uint32_t block_idx = pos / bs;
        uint32_t within = pos % bs;
        if (block_idx >= FS_DIRECT_BLOCKS || file.direct[block_idx] == 0) break;
        bd_read(&fs->bd, file.direct[block_idx], buf);
        uint32_t chunk = (uint32_t)((remaining < (bs - within)) ? remaining : (bs - within));
        memcpy(out + read, buf + within, chunk);
        remaining -= chunk;
        read += chunk;
        pos += chunk;
    }
    *bytes_read = read;
    return 0;
}

int fs_list_dir(fs_t *fs, uint32_t cwd_inode, const char *path, struct fs_dirent_disk **out_entries, size_t *out_count) {
    struct fs_inode dir;
    if (resolve_path(fs, cwd_inode, path, &dir, NULL) != 0) return -1;
    if (dir.type != FS_INODE_DIR) return -1;
    uint8_t *buf = NULL;
    if (dir_load(fs, &dir, &buf) != 0) return -1;
    *out_entries = (struct fs_dirent_disk *)buf;
    *out_count = dir.size / sizeof(struct fs_dirent_disk);
    return 0;
}
