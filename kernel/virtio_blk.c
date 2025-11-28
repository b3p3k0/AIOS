#include "virtio_blk.h"
#include "io.h"
#include "mem.h"
#include "util.h"
#include <stddef.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define VIRTQ_NUM 8
#define VIRTIO_SECTOR_SIZE 512u

#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_DEVICE_BLK 0x1001
#define VIRTIO_BLK_T_IN   0
#define VIRTIO_BLK_T_OUT  1

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01
#define VIRTIO_STATUS_DRIVER      0x02
#define VIRTIO_STATUS_DRIVER_OK   0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08

#define VIRTIO_REG_DEVICE_FEATURES 0x00
#define VIRTIO_REG_DRIVER_FEATURES 0x04
#define VIRTIO_REG_QUEUE_ADDRESS   0x08
#define VIRTIO_REG_QUEUE_SIZE      0x0C
#define VIRTIO_REG_QUEUE_SELECT    0x0E
#define VIRTIO_REG_QUEUE_NOTIFY    0x10
#define VIRTIO_REG_DEVICE_STATUS   0x12
#define VIRTIO_REG_ISR_STATUS      0x13
#define VIRTIO_REG_DEVICE_CONFIG   0x20

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_NUM];
    uint16_t used_event;
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_NUM];
    uint16_t avail_event;
};

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct virtio_blk_queue {
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
};

static inline void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((1u << 31) | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static inline uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)((1u << 31) | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl_port(PCI_CONFIG_DATA);
}

static inline uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = pci_read32(bus, device, function, offset);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

static inline void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t original = pci_read32(bus, device, function, offset);
    uint16_t shift = (offset & 2) * 8;
    uint32_t mask = 0xFFFFu << shift;
    uint32_t new_value = (original & ~mask) | ((uint32_t)value << shift);
    pci_write32(bus, device, function, offset, new_value);
}

static void write_status(uint16_t iobase, uint8_t status) {
    outb(iobase + VIRTIO_REG_DEVICE_STATUS, status);
}

static uint8_t read_status(uint16_t iobase) {
    return inb_port(iobase + VIRTIO_REG_DEVICE_STATUS);
}

static void queue_notify(uint16_t iobase, uint16_t queue) {
    outw(iobase + VIRTIO_REG_QUEUE_NOTIFY, queue);
}

static void queue_select(uint16_t iobase, uint16_t queue) {
    outw(iobase + VIRTIO_REG_QUEUE_SELECT, queue);
}

static uint16_t queue_size(uint16_t iobase) {
    return inw(iobase + VIRTIO_REG_QUEUE_SIZE);
}

static void queue_set_address(uint16_t iobase, uint32_t pfn) {
    outl(iobase + VIRTIO_REG_QUEUE_ADDRESS, pfn);
}

static uint64_t read_capacity(uint16_t iobase) {
    uint32_t low = inl_port(iobase + VIRTIO_REG_DEVICE_CONFIG + 0);
    uint32_t high = inl_port(iobase + VIRTIO_REG_DEVICE_CONFIG + 4);
    return ((uint64_t)high << 32) | low;
}

static int virtio_setup_queue(struct virtio_blk *dev) {
    queue_select(dev->iobase, 0);
    uint16_t qsz = queue_size(dev->iobase);
    if (qsz == 0) return -1;
    if (qsz > VIRTQ_NUM) qsz = VIRTQ_NUM;
    dev->queue_size = qsz;

    size_t alloc = sizeof(struct virtq_desc) * qsz +
                   sizeof(struct virtq_avail) +
                   sizeof(struct virtq_used);
    void *mem = kalloc_aligned(alloc, 0x1000);
    if (!mem) return -1;
    memset(mem, 0, alloc);

    dev->desc = (struct virtq_desc *)mem;
    dev->avail = (struct virtq_avail *)((uint8_t *)dev->desc + sizeof(struct virtq_desc) * qsz);
    uintptr_t used_ptr = (uintptr_t)(dev->avail + 1);
    used_ptr = (used_ptr + 3) & ~((uintptr_t)3);
    dev->used = (struct virtq_used *)used_ptr;
    dev->used_idx = 0;

    uintptr_t phys = (uintptr_t)dev->desc;
    queue_set_address(dev->iobase, (uint32_t)(phys >> 12));
    return 0;
}

static int virtio_find_device(struct virtio_blk *dev) {
    for (uint8_t bus = 0; bus < 32; ++bus) {
        for (uint8_t device = 0; device < 32; ++device) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor = pci_read16(bus, device, func, 0x00);
                if (vendor == 0xFFFF) continue;
                uint16_t device_id = pci_read16(bus, device, func, 0x02);
                if (vendor == VIRTIO_VENDOR && device_id == VIRTIO_DEVICE_BLK) {
                    dev->bus = bus;
                    dev->device = device;
                    dev->function = func;
                    return 0;
                }
            }
        }
    }
    return -1;
}

int virtio_blk_init(struct virtio_blk *dev) {
    if (virtio_find_device(dev) != 0) {
        return -1;
    }

    uint32_t bar0 = pci_read32(dev->bus, dev->device, dev->function, 0x10);
    uint16_t iobase = (uint16_t)(bar0 & ~0x3);
    dev->iobase = iobase;

    uint16_t command = pci_read16(dev->bus, dev->device, dev->function, 0x04);
    command |= (1 << 0) | (1 << 2); /* I/O + bus master */
    pci_write16(dev->bus, dev->device, dev->function, 0x04, command);

    write_status(iobase, 0);
    write_status(iobase, VIRTIO_STATUS_ACKNOWLEDGE);
    write_status(iobase, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    write_status(iobase, read_status(iobase) | VIRTIO_STATUS_FEATURES_OK);
    if (!(read_status(iobase) & VIRTIO_STATUS_FEATURES_OK)) {
        return -1;
    }

    if (virtio_setup_queue(dev) != 0) {
        return -1;
    }

    dev->request = (struct virtio_blk_req *)kalloc(sizeof(struct virtio_blk_req));
    dev->status = (uint8_t *)kalloc(1);
    if (!dev->request || !dev->status) return -1;

    dev->capacity_sectors = read_capacity(iobase);
    write_status(iobase, read_status(iobase) | VIRTIO_STATUS_DRIVER_OK);
    return 0;
}

static int virtio_blk_submit(struct virtio_blk *dev, uint32_t type, uint64_t sector, void *buf, uint32_t sectors, int write) {
    uint16_t idx = dev->avail->idx % dev->queue_size;
    struct virtio_blk_req *req = dev->request;
    req->type = type;
    req->reserved = 0;
    req->sector = sector;
    *dev->status = 0xFF;

    dev->desc[0].addr = (uint64_t)(uintptr_t)req;
    dev->desc[0].len = sizeof(*req);
    dev->desc[0].flags = VIRTQ_DESC_F_NEXT;
    dev->desc[0].next = 1;

    dev->desc[1].addr = (uint64_t)(uintptr_t)buf;
    dev->desc[1].len = sectors * VIRTIO_SECTOR_SIZE;
    dev->desc[1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    dev->desc[1].next = 2;

    dev->desc[2].addr = (uint64_t)(uintptr_t)dev->status;
    dev->desc[2].len = 1;
    dev->desc[2].flags = VIRTQ_DESC_F_WRITE;
    dev->desc[2].next = 0;

    dev->avail->ring[idx] = 0;
    dev->avail->idx++;
    queue_notify(dev->iobase, 0);

    uint32_t spin = 0;
    const uint32_t spin_limit = 1u << 24;
    while (dev->used->idx == dev->used_idx) {
        uint8_t isr = inb_port(dev->iobase + VIRTIO_REG_ISR_STATUS);
        if (isr & 0x1) break;
        if (++spin >= spin_limit) {
            return -1;
        }
        __asm__ __volatile__("pause");
    }
    dev->used_idx = dev->used->idx;
    if (*dev->status != 0) {
        return -1;
    }
    return 0;
}

int virtio_blk_read_sectors(struct virtio_blk *dev, uint64_t lba, void *buf, uint32_t sectors) {
    return virtio_blk_submit(dev, VIRTIO_BLK_T_IN, lba, buf, sectors, 0);
}

int virtio_blk_write_sectors(struct virtio_blk *dev, uint64_t lba, const void *buf, uint32_t sectors) {
    return virtio_blk_submit(dev, VIRTIO_BLK_T_OUT, lba, (void *)buf, sectors, 1);
}

struct virtio_block_ctx {
    struct virtio_blk *dev;
    uint32_t sectors_per_block;
};

static int virtio_read_block(struct blockdev *bd, uint32_t block, void *buf) {
    struct virtio_block_ctx *ctx = (struct virtio_block_ctx *)bd->ctx;
    uint64_t lba = (uint64_t)block * ctx->sectors_per_block;
    return virtio_blk_read_sectors(ctx->dev, lba, buf, ctx->sectors_per_block);
}

static int virtio_write_block(struct blockdev *bd, uint32_t block, const void *buf) {
    struct virtio_block_ctx *ctx = (struct virtio_block_ctx *)bd->ctx;
    uint64_t lba = (uint64_t)block * ctx->sectors_per_block;
    return virtio_blk_write_sectors(ctx->dev, lba, buf, ctx->sectors_per_block);
}

int bd_init_virtio(struct blockdev *bd, struct virtio_blk *dev, uint32_t block_size) {
    if (block_size % VIRTIO_SECTOR_SIZE != 0) return -1;
    struct virtio_block_ctx *ctx = (struct virtio_block_ctx *)kalloc(sizeof(struct virtio_block_ctx));
    if (!ctx) return -1;
    ctx->dev = dev;
    ctx->sectors_per_block = block_size / VIRTIO_SECTOR_SIZE;
    bd->ctx = ctx;
    bd->block_size = block_size;
    bd->blocks = dev->capacity_sectors / ctx->sectors_per_block;
    bd->read_fn = virtio_read_block;
    bd->write_fn = virtio_write_block;
    return 0;
}
