# AIOS Toy Filesystem + Shell

A minimal, single-user filesystem with a tiny interactive shell. Everything is host-side C code meant for experimentation, not a production FS. The code lives entirely in this directory and builds to a standalone binary.

## Features
- Persistent block device backed by a regular file.
- Simple on-disk layout: superblock → inode bitmap → inode table → data bitmap → data blocks.
- Fixed-size inodes with direct pointers only (no indirects/journaling/permissions).
- Flat directory entries stored inside directory data blocks; supports nested directories.
- Path resolution with `.` and `..` handling and both absolute/relative paths.
- Interactive shell with human-readable commands: `list`, `make-dir`, `delete`, `read`, `write`, `cd`, `pwd`, `format`, `mount`, `help`.

## Build
```bash
cd fs_shell
make
```
This produces `fs_shell` in the same directory. The code depends only on the C standard library and POSIX file APIs.

## Run
```bash
./fs_shell [disk_image_path]
```
If the image does not exist or lacks a valid superblock, the shell will format a new filesystem automatically (default: 4 MiB image with 1024 blocks and 256 inodes).

Example session:
```
$ ./fs_shell
AIOS toy filesystem shell. Using image fs_image.img
Commands:
  list [path]      - list directory contents
  make-dir <path>  - create directory
  delete <path>    - delete file or empty directory
  read <path>      - display file contents
  write <path>     - create/truncate file and read content from stdin (end with Ctrl-D)
  cd <path>        - change directory
  pwd              - print working directory
  format           - format current image (destructive)
  mount <image>    - mount a different image (formats if missing)
  help             - show this help
  exit             - quit shell
aios-fs:/> make-dir notes
aios-fs:/> write notes/todo
Enter content, end with Ctrl-D (EOF):
Add real MMU setup
Add paging tests
<Ctrl-D>
aios-fs:/> list notes
[file]	todo
aios-fs:/> read notes/todo
Add real MMU setup
Add paging tests
```

## On-Disk Layout
- **Block size:** 4096 bytes (fixed).
- **Block 0:** `fs_superblock` (magic, counts, offsets, root inode).
- **Blocks [1 .. inode_bitmap_blocks]:** inode bitmap (bit = allocated).
- **Blocks after bitmap:** inode table (array of `fs_inode`).
- **Next blocks:** data bitmap (bit = allocated data block).
- **Remaining blocks:** data region; inode direct pointers store absolute block numbers here.

### Key Structures
- `fs_superblock`: describes offsets/sizes for every region and the root inode number.
- `fs_inode`: type (file/dir), size in bytes, and up to 8 direct block pointers.
- `fs_dirent_disk`: directory entry (name, inode number, type). Inode number `0` marks an empty slot.

### Limits & Simplifications
- Maximum file size = `FS_DIRECT_BLOCKS * block_size` (8 * 4096 = 32 KiB).
- Fixed maximum filename length: 31 characters (stored in a 32-byte buffer).
- No permissions, timestamps, links, or journaling.
- No crash consistency guarantees; metadata is written immediately for simplicity.

## Testing
Use the shell commands themselves as a smoke test:
1. `format` to reinitialize the image.
2. `make-dir dir1`, `make-dir dir1/sub`, `write dir1/sub/file` then provide text.
3. `list dir1/sub` to verify contents.
4. `read dir1/sub/file` to confirm persistence.
5. Restart `./fs_shell` and check the data is still present.

## File Overview
- `fs.h` / `fs.c` — filesystem core (superblock/inodes/bitmaps, path resolution, file + directory ops).
- `blockdev.h` / `blockdev.c` — file-backed block device abstraction.
- `shell.c` — interactive command loop using the FS API.
- `Makefile` — builds the `fs_shell` binary.
