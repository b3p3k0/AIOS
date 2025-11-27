#include "shell.h"
#include "serial.h"
#include "util.h"
#include "mem.h"
#include "fs/fs.h"

#define LINE_MAX 256
#define TOKEN_MAX 8
#define PATH_MAX FS_MAX_PATH

static void print(const char *s) { serial_write(s); }

static int read_line(char *out, size_t max) {
    size_t len = 0;
    while (len + 1 < max) {
        int c = serial_getc();
        if (c == '\r' || c == '\n') {
            print("\r\n");
            break;
        } else if ((c == 0x7f || c == 0x08) && len) {
            len--;
            print("\b \b");
        } else if (c >= 0x20 && c <= 0x7E) {
            out[len++] = (char)c;
            char echo[2] = {(char)c, 0};
            print(echo);
        }
    }
    out[len] = '\0';
    return (int)len;
}

static int tokenize(char *line, char *argv[], int max_tokens) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_tokens) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

static void path_normalize(const char *cwd, const char *input, char *out) {
    static char comps[32][FS_MAX_NAME];
    const char *stack[32];
    int n = 0;
    if (input[0] != '/') {
        const char *p = cwd;
        const char *seg = p;
        while (1) {
            if (*p == '/' || *p == '\0') {
                size_t len = (size_t)(p - seg);
                if (len > 0) {
                    if (len >= FS_MAX_NAME) len = FS_MAX_NAME - 1;
                    for (size_t i = 0; i < len; ++i) comps[n][i] = seg[i];
                    comps[n][len] = '\0';
                    stack[n++] = comps[n - 1];
                }
                if (*p == '\0') break;
                seg = p + 1;
            }
            p++;
        }
    }
    const char *p = (input[0] == '/') ? input + 1 : input;
    const char *seg = p;
    while (1) {
        if (*p == '/' || *p == '\0') {
            size_t len = (size_t)(p - seg);
            if (len > 0) {
                if (len == 1 && seg[0] == '.') {
                    /* skip */
                } else if (len == 2 && seg[0] == '.' && seg[1] == '.') {
                    if (n > 0) n--;
                } else {
                    if (len >= FS_MAX_NAME) len = FS_MAX_NAME - 1;
                    for (size_t i = 0; i < len; ++i) comps[n][i] = seg[i];
                    comps[n][len] = '\0';
                    stack[n++] = comps[n];
                }
            }
            if (*p == '\0') break;
            seg = p + 1;
        }
        p++;
    }
    char *o = out;
    if (n == 0) {
        *o++ = '/';
        *o = '\0';
        return;
    }
    for (int i = 0; i < n; ++i) {
        *o++ = '/';
        const char *s = stack[i];
        while (*s) *o++ = *s++;
    }
    *o = '\0';
}

static int ensure_fs_ready(struct storage_state *storage) {
    if (!storage->fs_ready) {
        print("[fs] persistent disk not ready — run \"format-disk\" first\r\n");
        return 0;
    }
    return 1;
}

static void sysinfo_ram(const struct aios_boot_info *boot) {
    uint64_t total = boot->memory_summary.total_usable_bytes;
    uint64_t heap_total = mem_total();
    uint64_t heap_used = mem_used();
    print("Physical RAM: 0x");
    serial_write_hex(total);
    print(" bytes\r\n");
    print("Kernel heap: used ");
    serial_write_hex(heap_used);
    print(" / total ");
    serial_write_hex(heap_total);
    print(" bytes\r\n");
}

static void sysinfo_display(const struct aios_boot_info *boot) {
    print("Framebuffer base: 0x");
    serial_write_hex(boot->framebuffer.base);
    print("\r\nResolution: ");
    serial_write_u32(boot->framebuffer.width);
    print("x");
    serial_write_u32(boot->framebuffer.height);
    print(" px\r\nPitch: ");
    serial_write_u32(boot->framebuffer.pixels_per_scanline);
    print(" pixels per scanline\r\n");
}

static void sysinfo_storage(struct storage_state *storage) {
    if (storage->virtio_present) {
        print("Virtio disk: present ");
        if (storage->fs_ready && !storage->using_ram) {
            print("(mounted)\r\n");
        } else if (storage->needs_format) {
            print("(unformatted)\r\n");
        } else {
            print("(available)\r\n");
        }
        print("  Blocks: ");
        serial_write_u32(storage->virtio_dev.blocks);
        print(" of ");
        serial_write_u32(storage->virtio_dev.block_size);
        print(" bytes\r\n");
    } else {
        print("Virtio disk: not detected\r\n");
    }
    print("RAM seed: ");
    if (storage->ram_seed_present) {
        print("available (");
        serial_write_u32(storage->ram_seed_blocks);
        print(" blocks)\r\n");
    } else {
        print("not provided\r\n");
    }
    print("Active backend: ");
    print(storage->using_ram ? "RAM\r\n" : "virtio\r\n");
}

static void handle_sysinfo(struct shell_env *env, int argc, char **argv) {
    if (argc < 2) {
        print("usage: sysinfo <ram|storage|display>\r\n");
        return;
    }
    if (strcmp(argv[1], "ram") == 0) {
        sysinfo_ram(env->boot);
    } else if (strcmp(argv[1], "storage") == 0) {
        sysinfo_storage(env->storage);
    } else if (strcmp(argv[1], "display") == 0) {
        sysinfo_display(env->boot);
    } else {
        print("unknown sysinfo target\r\n");
    }
}

static int copy_seed_to_virtio(struct storage_state *storage) {
    if (!storage->ram_seed_present) return -1;
    if (storage->ram_dev.block_size != storage->virtio_dev.block_size) return -1;
    uint32_t blocks = storage->ram_dev.blocks;
    if (blocks > storage->virtio_dev.blocks) blocks = storage->virtio_dev.blocks;
    uint8_t *tmp = (uint8_t *)kalloc(storage->virtio_dev.block_size);
    if (!tmp) return -1;
    for (uint32_t b = 0; b < blocks; ++b) {
        if (bd_read(&storage->ram_dev, b, tmp) != 0) return -1;
        if (bd_write(&storage->virtio_dev, b, tmp) != 0) return -1;
    }
    return 0;
}

static void handle_format_disk(struct shell_env *env, int argc, char **argv, uint32_t *cwd, char *cwd_path) {
    struct storage_state *storage = env->storage;
    if (!storage->virtio_present) {
        print("format-disk: virtio disk not detected\r\n");
        return;
    }
    bool use_seed = (argc > 1 && strcmp(argv[1], "seed") == 0);
    if (use_seed && !storage->ram_seed_present) {
        print("format-disk: no seed image available\r\n");
        return;
    }
    if (use_seed) {
        if (copy_seed_to_virtio(storage) != 0) {
            print("format-disk: seed copy failed\r\n");
            return;
        }
    } else {
        if (fs_format(&storage->fs, &storage->virtio_dev, 512) != 0) {
            print("format-disk: format failed\r\n");
            return;
        }
    }
    if (fs_mount(&storage->fs, &storage->virtio_dev) != 0) {
        print("format-disk: mount failed\r\n");
        return;
    }
    storage->fs_ready = true;
    storage->needs_format = false;
    storage->using_ram = false;
    storage->active_dev = &storage->virtio_dev;
    *cwd = fs_root_inode(&storage->fs);
    strcpy(cwd_path, "/");
    print("virtio disk ready.\r\n");
}

void shell_run(struct shell_env *env) {
    struct storage_state *storage = env->storage;
    fs_t *fs = &storage->fs;
    uint32_t cwd = storage->fs_ready ? fs_root_inode(fs) : 0;
    char cwd_path[PATH_MAX];
    strcpy(cwd_path, storage->fs_ready ? "/" : "(unmounted)");

    if (storage->needs_format) {
        print("[fs] virtio disk is blank — run \"format-disk\" to initialize.\r\n");
    }

    print("AIOS FS shell ready. Type 'help' for commands.\r\n");

    char line[LINE_MAX];
    char *argv[TOKEN_MAX];
    int argc;

    while (1) {
        print("aios-fs:");
        print(cwd_path);
        print("> ");
        if (read_line(line, sizeof(line)) <= 0) continue;
        argc = tokenize(line, argv, TOKEN_MAX);
        if (argc == 0) continue;

        if (strcmp(argv[0], "exit") == 0) break;
        if (strcmp(argv[0], "help") == 0) {
            print("Commands: list, make-dir, delete, read, write, goin, pwd, format, format-disk [seed], sysinfo <target>, help, exit\r\n");
            continue;
        }
        if (strcmp(argv[0], "sysinfo") == 0) {
            handle_sysinfo(env, argc, argv);
            continue;
        }
        if (strcmp(argv[0], "format-disk") == 0) {
            handle_format_disk(env, argc, argv, &cwd, cwd_path);
            continue;
        }
        if (strcmp(argv[0], "pwd") == 0) {
            print(cwd_path);
            print("\r\n");
            continue;
        }
        if (strcmp(argv[0], "format") == 0) {
            if (!storage->fs_ready) {
                print("[fs] nothing mounted\r\n");
                continue;
            }
            if (fs_format(fs, storage->active_dev, 256) != 0 || fs_mount(fs, storage->active_dev) != 0) {
                print("format failed\r\n");
            } else {
                cwd = fs_root_inode(fs);
                strcpy(cwd_path, "/");
            }
            continue;
        }
        if (!ensure_fs_ready(storage)) continue;

        if (strcmp(argv[0], "list") == 0) {
            const char *path = (argc > 1) ? argv[1] : ".";
            struct fs_dirent_disk *ents;
            size_t count;
            if (fs_list_dir(fs, cwd, path, &ents, &count) != 0) {
                print("list failed\r\n");
                continue;
            }
            for (size_t i = 0; i < count; ++i) {
                if (ents[i].inode == 0) continue;
                print(ents[i].type == FS_INODE_DIR ? "[dir]\t" : "[file]\t");
                print(ents[i].name);
                print("\r\n");
            }
            continue;
        }
        if (strcmp(argv[0], "make-dir") == 0 && argc > 1) {
            if (fs_make_dir(fs, cwd, argv[1]) != 0) print("make-dir failed\r\n");
            continue;
        }
        if (strcmp(argv[0], "delete") == 0 && argc > 1) {
            if (fs_delete(fs, cwd, argv[1]) != 0) print("delete failed\r\n");
            continue;
        }
        if (strcmp(argv[0], "read") == 0 && argc > 1) {
            struct fs_inode node;
            uint32_t ino;
            if (fs_lookup(fs, cwd, argv[1], &node, &ino) != 0 || node.type != FS_INODE_FILE) {
                print("read: not found\r\n");
                continue;
            }
            size_t to_read = node.size;
            uint8_t *buf = (uint8_t *)kcalloc(1, to_read + 1);
            size_t got = 0;
            if (fs_read_file(fs, cwd, argv[1], buf, to_read, 0, &got) != 0) {
                print("read failed\r\n");
            } else {
                buf[got] = '\0';
                print((char *)buf);
                print("\r\n");
            }
            continue;
        }
        if (strcmp(argv[0], "write") == 0 && argc > 1) {
            print("Enter content, end with a single '.' line\r\n");
            uint8_t buffer[4096];
            size_t total = 0;
            while (total + 2 < sizeof(buffer)) {
                if (read_line(line, sizeof(line)) == 1 && line[0] == '.') break;
                size_t len = strlen(line);
                if (total + len + 1 >= sizeof(buffer)) break;
                memcpy(buffer + total, line, len);
                total += len;
                buffer[total++] = '\n';
            }
            if (fs_lookup(fs, cwd, argv[1], NULL, NULL) != 0) {
                if (fs_create_file(fs, cwd, argv[1]) != 0) {
                    print("write: create failed\r\n");
                    continue;
                }
            }
            if (fs_write_file(fs, cwd, argv[1], buffer, total, 0) != 0) {
                print("write failed\r\n");
            }
            continue;
        }
        if (strcmp(argv[0], "goin") == 0 && argc > 1) {
            struct fs_inode node;
            uint32_t ino;
            if (fs_lookup(fs, cwd, argv[1], &node, &ino) != 0 || node.type != FS_INODE_DIR) {
                print("goin failed\r\n");
                continue;
            }
            char new_path[PATH_MAX];
            path_normalize(cwd_path, argv[1], new_path);
            strncpy(cwd_path, new_path, PATH_MAX - 1);
            cwd_path[PATH_MAX - 1] = '\0';
            cwd = ino;
            continue;
        }

        print("Unknown command\r\n");
    }
}
