#include <stdint.h>
#include <stddef.h>
#include "aios/bootinfo.h"
#include "kernel/serial.h"
#include "kernel/util.h"
#include "kernel/mem.h"
#include "fs/fs.h"
#include "kernel/shell.h"

static uint32_t checksum_bootinfo(const struct aios_boot_info *boot) {
    struct aios_boot_info tmp = *boot;
    tmp.checksum = 0;
    const uint32_t *words = (const uint32_t *)&tmp;
    size_t count = sizeof(tmp) / sizeof(uint32_t);
    uint32_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum ^= words[i];
    }
    return sum;
}

void kernel_entry(struct aios_boot_info *boot) {
    serial_init();
    serial_write("[kernel] Firmware -> Loader -> Kernel -> [paging soon]\r\n");
    serial_write("[kernel] Stage: kernel entry\r\n");

    if (boot == 0) {
        serial_write("[kernel] Boot info missing; halting\r\n");
        goto halt;
    }

    uint32_t expected = checksum_bootinfo(boot);
    if (boot->magic != AIOS_BOOTINFO_MAGIC || boot->checksum != expected) {
        serial_write("[kernel] Boot info validation FAILED\r\n");
    } else {
        serial_write("[kernel] Boot info validation OK\r\n");
    }

    serial_write("[kernel] Accel: ");
    serial_write(boot->accel_mode);
    serial_write("\r\n");

    serial_write("[kernel] Kernel load base: 0x");
    serial_write_hex(boot->kernel_base);
    serial_write(" size: 0x");
    serial_write_hex(boot->kernel_size);
    serial_write("\r\n");

    serial_write("[kernel] Framebuffer: ");
    serial_write_hex(boot->framebuffer.base);
    serial_write(" ");
    serial_write_u32(boot->framebuffer.width);
    serial_write("x");
    serial_write_u32(boot->framebuffer.height);
    serial_write("\r\n");

    serial_write("[kernel] RAM usable total: 0x");
    serial_write_hex(boot->memory_summary.total_usable_bytes);
    serial_write(" largest: 0x");
    serial_write_hex(boot->memory_summary.largest_usable_base);
    serial_write(" (size 0x");
    serial_write_hex(boot->memory_summary.largest_usable_size);
    serial_write(")\r\n");

    serial_write("[kernel] Memory map buffer @ 0x");
    serial_write_hex(boot->memory_map.buffer);
    serial_write(" bytes: 0x");
    serial_write_hex(boot->memory_map.size);
    serial_write("\r\n");

    serial_write("[kernel] RSDP: 0x");
    serial_write_hex(boot->rsdp_address);
    serial_write("\r\n");

    serial_write("[kernel] Boot media: 0x");
    serial_write_hex(boot->boot_device.total_bytes);
    serial_write(" block ");
    serial_write_hex(boot->boot_device.block_size);
    serial_write(" ");
    serial_write(boot->boot_device.removable ? "removable" : "fixed");
    serial_write("\r\n");

    serial_write("\r\n");
    serial_write("          _____                    _____                   _______                   _____          \r\n");
    serial_write("         /\\    \\                  /\\    \\                 /::\\    \\                 /\\    \\         \r\n");
    serial_write("        /::\\    \\                /::\\    \\               /::::\\    \\               /::\\    \\        \r\n");
    serial_write("       /::::\\    \\               \\:::\\    \\             /::::::\\    \\             /::::\\    \\       \r\n");
    serial_write("      /::::::\\    \\               \\:::\\    \\           /::::::::\\    \\           /::::::\\    \\      \r\n");
    serial_write("     /:::/\\:::\\    \\               \\:::\\    \\         /:::/~~\\:::\\    \\         /:::/\\:::\\    \\     \r\n");
    serial_write("    /:::/__\\:::\\    \\               \\:::\\    \\       /:::/    \\:::\\    \\       /:::/__\\:::\\    \\    \r\n");
    serial_write("   /::::\\   \\:::\\    \\              /::::\\    \\     /:::/    / \\:::\\    \\      \\:::\\   \\:::\\    \\   \r\n");
    serial_write("  /::::::\\   \\:::\\    \\    ____    /::::::\\    \\   /:::/____/   \\:::\\____\\   ___\\:::\\   \\:::\\    \\  \r\n");
    serial_write(" /:::/\\:::\\   \\:::\\    \\  /\\   \\  /:::/\\:::\\    \\ |:::|    |     |:::|    | /\\   \\:::\\   \\:::\\    \\ \r\n");
    serial_write("/:::/  \\:::\\   \\:::\\____\\/::\\   \\/:::/  \\:::\\____\\|:::|____|     |:::|    |/::\\   \\:::\\   \\:::\\____\\\r\n");
    serial_write("\\::/    \\:::\\  /:::/    /\\:::\\  /:::/    \\::/    / \\:::\\    \\   /:::/    / \\:::\\   \\:::\\   \\::/    /\r\n");
    serial_write(" \\/____/ \\:::\/:::/    /  \\:::\/:::/    / \\/____/   \\:::\\    \\ /:::/    /   \\:::\\   \\:::\\   \\/____/ \r\n");
    serial_write("          \\::::::/    /    \\::::::/    /             \\:::\\    /:::/    /     \\:::\\   \\:::\\    \\     \r\n");
    serial_write("           \\::::/    /      \\::::/____/               \\:::\\__/:::/    /       \\:::\\   \\:::\\____\\    \r\n");
    serial_write("           /:::/    /        \\:::\\    \\                \\::::::::/    /         \\:::\\  /:::/    /    \r\n");
    serial_write("          /:::/    /          \\:::\\    \\                \\::::::/    /           \\:::\/:::/    /     \r\n");
    serial_write("         /:::/    /            \\:::\\    \\                \\::::/    /             \\::::::/    /      \r\n");
    serial_write("        /:::/    /              \\:::\\____\\                \\::/____/               \\::::/    /       \r\n");
    serial_write("        \\::/    /                \\::/    /                 ~~                      \\::/    /        \r\n");
    serial_write("         \\/____/                  \\/____/                                           \\/____/         \r\n");
    serial_write("                        minimal hardware, maximal clarity\r\n\r\n");

    /* Initialize bump heap */
    static uint8_t heap_area[256 * 1024];
    mem_init(heap_area, sizeof(heap_area));

    fs_t fs;
    static uint8_t fs_fallback[4 * 1024 * 1024];
    void *fs_base = (boot->fs_image_base && boot->fs_image_size) ? (void *)(uintptr_t)boot->fs_image_base : fs_fallback;
    uint32_t fs_bytes = (boot->fs_image_base && boot->fs_image_size) ? (uint32_t)boot->fs_image_size : (uint32_t)sizeof(fs_fallback);
    int mounted = -1;
    mounted = fs_mount_ram(&fs, fs_base, fs_bytes);
    if (mounted != 0) {
        serial_write("[kernel] No FS image; formatting fresh RAM FS\r\n");
        uint32_t bytes = fs_bytes;
        if (fs_format_ram(&fs, fs_base, bytes, 256, FS_DEFAULT_BLOCK_SIZE) != 0) {
            serial_write("[kernel] FS format failed; halting\r\n");
            goto halt;
        }
    }

    shell_run(&fs, fs_root_inode(&fs), "/");

halt:
    for (;;) { __asm__ __volatile__("hlt"); }
}
