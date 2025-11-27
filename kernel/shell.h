#ifndef AIOS_KERNEL_SHELL_H
#define AIOS_KERNEL_SHELL_H

#include "fs/fs.h"

void shell_run(fs_t *fs, uint32_t start_inode, const char *start_path);

#endif
