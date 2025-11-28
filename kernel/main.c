#include <stdint.h>
#include <stddef.h>
#include "aios/bootinfo.h"
#include "kernel/serial.h"
#include "kernel/util.h"
#include "kernel/mem.h"
#include "fs/fs.h"
#include "kernel/shell.h"
#include "virtio_blk.h"

static void print_ascii_banner(void) {
    serial_write(" ________  ___  ________  ________\r\n");
    serial_write("|\\   __  \\|\\  \\|\\   __  \\|\\   ____\\\r\n");
    serial_write("\\ \\  \\\|\\  \\ \\  \\ \\  \\\|\\  \\ \\  \\\___|_    \r\n");
    serial_write(" \\ \\   __  \\ \\  \\ \\  \\\\  \\ \\_____  \\\r\n");
    serial_write("  \\ \\  \\ \\  \\ \\  \\ \\  \\\\  \\\\|____|\\  \\\r\n");
    serial_write("   \\ \\__\\ \\__\\ \\__\\ \\_______\\____\\_\\  \\ \r\n");
    serial_write("    \\|__|\\|__|\\|__|\\|_______|\\_________\\\r\n");
    serial_write("                            \\|_________|\r\n\r\n");
}

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
    print_ascii_banner();
    serial_write("Welcome to AIOS — minimal hardware, maximal clarity.\r\n\r\n");

    /* Initialize bump heap */
    static uint8_t heap_area[256 * 1024];
    mem_init(heap_area, sizeof(heap_area));

    static uint8_t fs_fallback[4 * 1024 * 1024];
    void *seed_base = (boot->fs_image_base && boot->fs_image_size) ? (void *)(uintptr_t)boot->fs_image_base : fs_fallback;
    uint32_t seed_bytes = (boot->fs_image_base && boot->fs_image_size) ? (uint32_t)boot->fs_image_size : (uint32_t)sizeof(fs_fallback);

    struct storage_state storage;
    memset(&storage, 0, sizeof(storage));

    if (bd_init_ram(&storage.ram_dev, seed_base, seed_bytes, FS_DEFAULT_BLOCK_SIZE) != 0) {
        serial_write("[kernel] RAM disk init failed\r\n");
        goto halt;
    }
    storage.ram_seed_present = (boot->fs_image_base && boot->fs_image_size);
    storage.ram_seed_blocks = storage.ram_dev.blocks;
    storage.ram_seed_block_size = storage.ram_dev.block_size;

    if (virtio_blk_init(&storage.virtio) == 0) {
        if (bd_init_virtio(&storage.virtio_dev, &storage.virtio, FS_DEFAULT_BLOCK_SIZE) == 0) {
            storage.virtio_present = true;
            storage.active_dev = &storage.virtio_dev;
            if (fs_mount(&storage.fs, &storage.virtio_dev) == 0) {
                storage.fs_ready = true;
                storage.using_ram = false;
            } else {
                storage.needs_format = true;
            }
        }
    }

    if (!storage.virtio_present) {
        if (fs_mount(&storage.fs, &storage.ram_dev) != 0) {
            serial_write("[kernel] No FS image; formatting RAM FS\r\n");
            if (fs_format(&storage.fs, &storage.ram_dev, 256) != 0 || fs_mount(&storage.fs, &storage.ram_dev) != 0) {
                serial_write("[kernel] RAM FS setup failed; halting\r\n");
                goto halt;
            }
        }
        storage.fs_ready = true;
        storage.using_ram = true;
        storage.active_dev = &storage.ram_dev;
    }

    struct shell_env env = {
        .storage = &storage,
        .boot = boot
    };
    shell_run(&env);

halt:
    for (;;) { __asm__ __volatile__("hlt"); }
}
