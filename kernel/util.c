#include "kernel/util.h"

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *dest, int value, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) {
        *d++ = (uint8_t)value;
    }
    return dest;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s && *s++) {
        ++len;
    }
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n-- && *a && (*a == *b)) {
        a++; b++;
    }
    if ((int)n < 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++)) {}
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i) {
        dest[i] = src[i];
    }
    for (; i < n; ++i) {
        dest[i] = '\0';
    }
    return dest;
}
