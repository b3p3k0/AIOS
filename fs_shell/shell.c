#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fs.h"

#define DEFAULT_IMAGE "fs_image.img"
#define DEFAULT_BLOCKS 1024u      /* 4 MiB at 4096-byte blocks */
#define DEFAULT_INODES 256u

struct shell_state {
    fs_t fs;
    char image[256];
    uint32_t cwd_inode;
    char cwd_path[FS_MAX_PATH];
    int mounted;
};

static void trim_newline(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    if (len && s[len - 1] == '\n') {
        s[len - 1] = '\0';
    }
}

static int normalize_path(const char *cwd, const char *input, char *out) {
    char merged[FS_MAX_PATH];
    if (input[0] == '/') {
        if (strlen(input) >= sizeof(merged)) return -1;
        strcpy(merged, input);
    } else {
        size_t need = strlen(cwd) + 1 + strlen(input) + 1;
        if (need >= sizeof(merged)) return -1;
        if (strcmp(cwd, "/") == 0) {
            int n = snprintf(merged, sizeof(merged), "/%s", input);
            if (n < 0 || n >= (int)sizeof(merged)) return -1;
        } else {
            int n = snprintf(merged, sizeof(merged), "%s/%s", cwd, input);
            if (n < 0 || n >= (int)sizeof(merged)) return -1;
        }
    }

    const char *p = merged;
    char parts[64][FS_MAX_NAME];
    int depth = 0;
    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        char comp[FS_MAX_NAME];
        int len = 0;
        while (*p && *p != '/' && len < FS_MAX_NAME - 1) {
            comp[len++] = *p++;
        }
        comp[len] = '\0';
        while (*p == '/') p++;
        if (strcmp(comp, ".") == 0) {
            continue;
        }
        if (strcmp(comp, "..") == 0) {
            if (depth > 0) depth--;
            continue;
        }
        if (depth < 64) {
            strcpy(parts[depth++], comp);
        } else {
            return -1;
        }
    }
    if (depth == 0) {
        strcpy(out, "/");
        return 0;
    }
    out[0] = '\0';
    for (int i = 0; i < depth; ++i) {
        strcat(out, "/");
        strcat(out, parts[i]);
    }
    return 0;
}

static void print_help(void) {
    printf("Commands:\n");
    printf("  list [path]      - list directory contents\n");
    printf("  make-dir <path>  - create directory\n");
    printf("  delete <path>    - delete file or empty directory\n");
    printf("  read <path>      - display file contents\n");
    printf("  write <path>     - create/truncate file and read content from stdin (end with Ctrl-D)\n");
    printf("  cd <path>        - change directory\n");
    printf("  pwd              - print working directory\n");
    printf("  format           - format current image (destructive)\n");
    printf("  mount <image>    - mount a different image (formats if missing)\n");
    printf("  help             - show this help\n");
    printf("  exit             - quit shell\n");
}

static int ensure_mounted(struct shell_state *sh) {
    if (sh->mounted) return 0;
    if (fs_mount(&sh->fs, sh->image) == 0) {
        sh->mounted = 1;
        sh->cwd_inode = fs_root_inode(&sh->fs);
        strcpy(sh->cwd_path, "/");
        return 0;
    }
    printf("No filesystem found on %s, creating one...\n", sh->image);
    if (fs_format(&sh->fs, sh->image, DEFAULT_BLOCKS, DEFAULT_INODES, FS_DEFAULT_BLOCK_SIZE) != 0) {
        return -1;
    }
    sh->mounted = 1;
    sh->cwd_inode = fs_root_inode(&sh->fs);
    strcpy(sh->cwd_path, "/");
    return 0;
}

static int cmd_list(struct shell_state *sh, const char *arg) {
    if (ensure_mounted(sh) != 0) return -1;
    const char *path = arg ? arg : ".";
    struct fs_dirent_disk *entries = NULL;
    size_t count = 0;
    if (fs_list_dir(&sh->fs, sh->cwd_inode, path, &entries, &count) != 0) {
        printf("list: failed\n");
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        printf("%s\t%s\n", entries[i].type == FS_INODE_DIR ? "[dir]" : "[file]", entries[i].name);
    }
    free(entries);
    return 0;
}

static int cmd_mkdir(struct shell_state *sh, const char *arg) {
    if (!arg) { printf("make-dir: missing path\n"); return -1; }
    if (ensure_mounted(sh) != 0) return -1;
    if (fs_make_dir(&sh->fs, sh->cwd_inode, arg) != 0) {
        printf("make-dir: failed\n");
        return -1;
    }
    return 0;
}

static int cmd_delete(struct shell_state *sh, const char *arg) {
    if (!arg) { printf("delete: missing path\n"); return -1; }
    if (ensure_mounted(sh) != 0) return -1;
    if (fs_delete(&sh->fs, sh->cwd_inode, arg) != 0) {
        printf("delete: failed (directory not empty or not found)\n");
        return -1;
    }
    return 0;
}

static int ensure_file_exists(struct shell_state *sh, const char *path) {
    struct fs_inode node;
    uint32_t ino;
    if (fs_lookup(&sh->fs, sh->cwd_inode, path, &node, &ino) == 0) {
        if (node.type != FS_INODE_FILE) {
            return -1;
        }
        return 0;
    }
    return fs_create_file(&sh->fs, sh->cwd_inode, path);
}

static int cmd_write(struct shell_state *sh, const char *arg) {
    if (!arg) { printf("write: missing path\n"); return -1; }
    if (ensure_mounted(sh) != 0) return -1;
    if (ensure_file_exists(sh, arg) != 0) {
        printf("write: failed to create file\n");
        return -1;
    }
    printf("Enter content, end with Ctrl-D (EOF):\n");
    char *line = NULL;
    size_t cap = 0;
    size_t total = 0;
    size_t buf_cap = 1024;
    char *buf = malloc(buf_cap);
    if (!buf) return -1;
    ssize_t n;
    while ((n = getline(&line, &cap, stdin)) != -1) {
        if (total + (size_t)n > buf_cap) {
            buf_cap = (total + (size_t)n) * 2;
            char *tmp = realloc(buf, buf_cap);
            if (!tmp) { free(buf); free(line); return -1; }
            buf = tmp;
        }
        memcpy(buf + total, line, (size_t)n);
        total += (size_t)n;
    }
    free(line);
    if (fs_write_file(&sh->fs, sh->cwd_inode, arg, (uint8_t *)buf, total, 0) != 0) {
        printf("write: failed to write data\n");
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

static int cmd_read(struct shell_state *sh, const char *arg) {
    if (!arg) { printf("read: missing path\n"); return -1; }
    if (ensure_mounted(sh) != 0) return -1;
    uint32_t ino;
    struct fs_inode node;
    if (fs_lookup(&sh->fs, sh->cwd_inode, arg, &node, &ino) != 0 || node.type != FS_INODE_FILE) {
        printf("read: not found or not a file\n");
        return -1;
    }
    size_t len = node.size;
    uint8_t *buf = malloc(len + 1);
    if (!buf) return -1;
    size_t got = 0;
    if (fs_read_file(&sh->fs, sh->cwd_inode, arg, buf, len, 0, &got) != 0) {
        printf("read: failed\n");
        free(buf);
        return -1;
    }
    buf[got] = '\0';
    fwrite(buf, 1, got, stdout);
    if (got == 0 || buf[got - 1] != '\n') {
        putchar('\n');
    }
    free(buf);
    return 0;
}

static int cmd_cd(struct shell_state *sh, const char *arg) {
    if (!arg) { printf("cd: missing path\n"); return -1; }
    if (ensure_mounted(sh) != 0) return -1;
    uint32_t ino;
    struct fs_inode node;
    if (fs_lookup(&sh->fs, sh->cwd_inode, arg, &node, &ino) != 0 || node.type != FS_INODE_DIR) {
        printf("cd: not found or not a directory\n");
        return -1;
    }
    char new_path[FS_MAX_PATH];
    if (normalize_path(sh->cwd_path, arg, new_path) != 0) {
        printf("cd: path too long\n");
        return -1;
    }
    sh->cwd_inode = ino;
    strncpy(sh->cwd_path, new_path, sizeof(sh->cwd_path) - 1);
    sh->cwd_path[sizeof(sh->cwd_path) - 1] = '\0';
    return 0;
}

static int cmd_pwd(struct shell_state *sh) {
    if (ensure_mounted(sh) != 0) return -1;
    printf("%s\n", sh->cwd_path);
    return 0;
}

static int cmd_format(struct shell_state *sh) {
    printf("Format will destroy all data on %s. Continue? (yes/no): ", sh->image);
    fflush(stdout);
    char reply[16];
    if (!fgets(reply, sizeof(reply), stdin)) return -1;
    trim_newline(reply);
    if (strcmp(reply, "yes") != 0) {
        printf("format cancelled\n");
        return 0;
    }
    fs_unmount(&sh->fs);
    if (fs_format(&sh->fs, sh->image, DEFAULT_BLOCKS, DEFAULT_INODES, FS_DEFAULT_BLOCK_SIZE) != 0) {
        printf("format: failed\n");
        return -1;
    }
    sh->mounted = 1;
    sh->cwd_inode = fs_root_inode(&sh->fs);
    strcpy(sh->cwd_path, "/");
    return 0;
}

static int cmd_mount(struct shell_state *sh, const char *arg) {
    if (!arg) { printf("mount: missing image path\n"); return -1; }
    fs_unmount(&sh->fs);
    strncpy(sh->image, arg, sizeof(sh->image) - 1);
    sh->image[sizeof(sh->image) - 1] = '\0';
    sh->mounted = 0;
    if (ensure_mounted(sh) != 0) {
        printf("mount: failed\n");
        return -1;
    }
    return 0;
}

static void dispatch(struct shell_state *sh, char *line) {
    trim_newline(line);
    char *cmd = strtok(line, " \t");
    if (!cmd) return;
    char *arg = strtok(NULL, " \t");

    if (strcmp(cmd, "help") == 0) { print_help(); return; }
    if (strcmp(cmd, "list") == 0) { cmd_list(sh, arg); return; }
    if (strcmp(cmd, "make-dir") == 0) { cmd_mkdir(sh, arg); return; }
    if (strcmp(cmd, "delete") == 0) { cmd_delete(sh, arg); return; }
    if (strcmp(cmd, "read") == 0) { cmd_read(sh, arg); return; }
    if (strcmp(cmd, "write") == 0) { cmd_write(sh, arg); return; }
    if (strcmp(cmd, "cd") == 0) { cmd_cd(sh, arg); return; }
    if (strcmp(cmd, "pwd") == 0) { cmd_pwd(sh); return; }
    if (strcmp(cmd, "format") == 0) { cmd_format(sh); return; }
    if (strcmp(cmd, "mount") == 0) { cmd_mount(sh, arg); return; }
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        fs_unmount(&sh->fs);
        exit(0);
    }
    printf("Unknown command. Type 'help'.\n");
}

int main(int argc, char **argv) {
    struct shell_state sh;
    memset(&sh, 0, sizeof(sh));
    sh.cwd_inode = 0;
    strcpy(sh.image, argc > 1 ? argv[1] : DEFAULT_IMAGE);
    sh.mounted = 0;
    strcpy(sh.cwd_path, "/");

    printf("AIOS toy filesystem shell. Using image %s\n", sh.image);
    print_help();

    char *line = NULL;
    size_t cap = 0;
    while (1) {
        printf("aios-fs:%s> ", sh.cwd_path);
        fflush(stdout);
        ssize_t n = getline(&line, &cap, stdin);
        if (n == -1) {
            putchar('\n');
            break;
        }
        dispatch(&sh, line);
    }
    fs_unmount(&sh.fs);
    free(line);
    return 0;
}
