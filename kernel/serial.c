#include "kernel/serial.h"

#define COM1_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_wait(void) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0) {
        __asm__ __volatile__("pause");
    }
}

static int serial_ready(void) {
    return inb(COM1_PORT + 5) & 0x01;
}

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

void serial_write(const char *str) {
    while (*str) {
        if (*str == '\n') {
            serial_wait();
            outb(COM1_PORT, '\r');
        }
        serial_wait();
        outb(COM1_PORT, (uint8_t)*str++);
    }
}

int serial_getc(void) {
    while (!serial_ready()) {
        __asm__ __volatile__("pause");
    }
    return inb(COM1_PORT);
}

static const char HEX_TABLE[] = "0123456789ABCDEF";

void serial_write_hex(uint64_t value) {
    serial_write("0x");
    for (int i = 60; i >= 0; i -= 4) {
        char c = HEX_TABLE[(value >> i) & 0xF];
        serial_wait();
        outb(COM1_PORT, c);
    }
}

void serial_write_u32(uint32_t value) {
    char buffer[16];
    int idx = 0;
    if (value == 0) {
        buffer[idx++] = '0';
    } else {
        uint32_t temp = value;
        char reversed[16];
        int r = 0;
        while (temp > 0 && r < 15) {
            reversed[r++] = '0' + (temp % 10);
            temp /= 10;
        }
        while (r > 0) {
            buffer[idx++] = reversed[--r];
        }
    }
    buffer[idx] = '\0';
    serial_write(buffer);
}
