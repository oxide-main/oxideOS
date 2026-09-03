#ifndef STRING_H
#define STRING_H

#include "types.h"

static inline void *memset(void *s, int c, uint32_t n)
{
    uint8_t *p = (uint8_t *) s;
    while (n--) {
        *p++ = (uint8_t) c;
    }
    return s;
}

static inline void *memcpy(void *dest, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dest;
    const uint8_t *s2 = (const uint8_t *) src;
    while (n--) {
        *d++ = *s2++;
    }
    return dest;
}

static inline uint32_t strlen(const char *s)
{
    uint32_t len = 0;
    while (s && s[len] != '\0') {
        len++;
    }
    return len;
}

static inline int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

#endif
