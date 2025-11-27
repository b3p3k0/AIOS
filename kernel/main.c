#include <stdint.h>
#include "aios/bootinfo.h"
#include "kernel/serial.h"
#include "kernel/util.h"

void kernel_entry(struct aios_boot_info *boot) {
    serial_init();
    serial_write("AIOS kernel stub online\r\n");
    if (boot == 0 || boot->magic != AIOS_BOOTINFO_MAGIC) {
        serial_write("Boot info missing or corrupted\r\n");
        goto halt;
    }

    serial_write("Kernel load base: 0x");
    serial_write_hex(boot->kernel_base);
    serial_write(" size: 0x");
    serial_write_hex(boot->kernel_size);
    serial_write("\r\n");

    serial_write("Framebuffer: ");
    serial_write_hex(boot->framebuffer.base);
    serial_write(" ");
    serial_write_u32(boot->framebuffer.width);
    serial_write("x");
    serial_write_u32(boot->framebuffer.height);
    serial_write("\r\n");

    serial_write("Memory map bytes: 0x");
    serial_write_hex(boot->memory_map.size);
    serial_write(" entries size: 0x");
    serial_write_hex(boot->memory_map.descriptor_size);
    serial_write("\r\n");

halt:
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
