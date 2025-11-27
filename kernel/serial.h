#ifndef AIOS_KERNEL_SERIAL_H
#define AIOS_KERNEL_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write(const char *str);
void serial_write_hex(uint64_t value);
void serial_write_u32(uint32_t value);
int serial_getc(void);

#endif
