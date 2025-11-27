#include "shell.h"
#include "serial.h"
#include "util.h"
#include "mem.h"

#define LINE_MAX 256
#define TOKEN_MAX 8
#define PATH_MAX FS_MAX_PATH

static void print(const char *s) { serial_write(s); }

static void print_hex64(uint64_t v) { serial_write_hex(v); }

static int read_line(char *out, size_t max) {
    size_t len = 0;
    while (len + 1 < max) {
        int c = serial_getc();
        if (c == '\r' || c == '\n') {
            print("\r\n");
            break;
        } else if (c == 0x7f || c == 0x08) { /* backspace */
            if (len) {
                len--;
                print("\b \b");
            }
        } else {
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

static void show_help(void) {
    print("Commands:\r\n");
    print("  list [path]\r\n");
    print("  make-dir <path>\r\n");
    print("  delete <path>\r\n");
    print("  read <path>\r\n");
    print("  write <path>   (end input with a single '.' line)\r\n");
    print("  cd <path>\r\n");
    print("  pwd\r\n");
    print("  format\r\n");
    print("  help\r\n");
    print("  exit\r\n");
}

static void path_normalize(const char *cwd, const char *input, char *out) {
    static char comps[32][FS_MAX_NAME];
    const char *stack[32];
    int n = 0;
    /* seed with cwd components if relative */
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
                    stack[n] = comps[n];
                    n++;
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
                    stack[n] = comps[n];
                    n++;
                }
            }
            if (*p == '\0') break;
            seg = p + 1;
        }
        p++;
    }
    char *o = out;
    if (n == 0) { *o++ = '/'; *o = '\0'; return; }
    for (int i = 0; i < n; ++i) {
        *o++ = '/';
        const char *s = stack[i];
        while (*s) *o++ = *s++;
    }
    *o = '\0';
}

void shell_run(fs_t *fs, uint32_t start_inode, const char *start_path) {
    char line[LINE_MAX];
    char *argv[TOKEN_MAX];
    int argc;
    uint32_t cwd = start_inode;
    char cwd_path[PATH_MAX];
    strncpy(cwd_path, start_path, PATH_MAX - 1);
    cwd_path[PATH_MAX - 1] = '\0';

    print("AIOS kernel FS shell. Type 'help' for commands.\r\n");
    while (1) {
        print("aios-fs:");
        print(cwd_path);
        print("> ");
        int len = read_line(line, sizeof(line));
        if (len <= 0) continue;
        argc = tokenize(line, argv, TOKEN_MAX);
        if (argc == 0) continue;
        if (strcmp(argv[0], "help") == 0) { show_help(); continue; }
        if (strcmp(argv[0], "exit") == 0) { break; }
        if (strcmp(argv[0], "pwd") == 0) { print(cwd_path); print("\r\n"); continue; }
        if (strcmp(argv[0], "list") == 0) {
            const char *path = (argc > 1) ? argv[1] : ".";
            struct fs_dirent_disk *ents;
            size_t count;
            if (fs_list_dir(fs, cwd, path, &ents, &count) != 0) { print("list failed\r\n"); continue; }
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
                print("read: not found\r\n"); continue;
            }
            size_t to_read = node.size;
            uint8_t *buf = kcalloc(1, to_read + 1);
            size_t got = 0;
            if (fs_read_file(fs, cwd, argv[1], buf, to_read, 0, &got) != 0) { print("read failed\r\n"); continue; }
            buf[got] = '\0';
            print((char *)buf);
            print("\r\n");
            continue;
        }
        if (strcmp(argv[0], "write") == 0 && argc > 1) {
            print("Enter content, end with a single '.' line\r\n");
            char buf[2048];
            size_t total = 0;
            while (total + 2 < sizeof(buf)) {
                int l = read_line(line, sizeof(line));
                if (l == 1 && line[0] == '.') break;
                if (total + l + 1 >= sizeof(buf)) break;
                memcpy(buf + total, line, l);
                total += l;
                buf[total++] = '\n';
            }
            if (fs_lookup(fs, cwd, argv[1], NULL, NULL) != 0) {
                if (fs_create_file(fs, cwd, argv[1]) != 0) { print("write: create failed\r\n"); continue; }
            }
            if (fs_write_file(fs, cwd, argv[1], (uint8_t *)buf, total, 0) != 0) print("write failed\r\n");
            continue;
        }
        if (strcmp(argv[0], "cd") == 0 && argc > 1) {
            struct fs_inode node;
            uint32_t ino;
            if (fs_lookup(fs, cwd, argv[1], &node, &ino) != 0 || node.type != FS_INODE_DIR) {
                print("cd failed\r\n"); continue;
            }
            char new_path[PATH_MAX];
            path_normalize(cwd_path, argv[1], new_path);
            strncpy(cwd_path, new_path, PATH_MAX-1); cwd_path[PATH_MAX-1]=0;
            cwd = ino;
            continue;
        }
        if (strcmp(argv[0], "format") == 0) {
            if (fs_format_ram(fs, fs->bd.base, fs->bd.blocks * fs->bd.block_size, 256, fs->bd.block_size) != 0) {
                print("format failed\r\n");
            }
            cwd = fs_root_inode(fs);
            strcpy(cwd_path, "/");
            continue;
        }
        print("Unknown or invalid command\r\n");
    }
}
