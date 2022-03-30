/*
 * @Author: CALM.WU
 * @Date: 2021-11-19 10:46:05
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-19 13:59:49
 */

#include "strings.h"
#include "compiler.h"

int32_t str_split_to_nums(const char *str, const char *delim, uint64_t *nums,
                          uint16_t nums_max_size) {
    int32_t  count = 0;
    uint64_t num = 0;

    if (str == NULL || delim == NULL || nums == NULL || strlen(str) == 0 || strlen(delim) == 0) {
        return -EINVAL;
    }

    for (char *tok = strtok((char *restrict)str, delim); tok != NULL && count < nums_max_size;
         tok = strtok(NULL, delim), count++) {
        num = strtoull(tok, NULL, 10);
        nums[count] = num;
    }

    return count;
}

int32_t str2int32_t(const char *s) {
    int32_t n = 0;
    char    c, negative = (*s == '-');

    for (c = (negative) ? *(++s) : *s; c >= '0' && c <= '9'; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }

    if (unlikely(negative))
        return -n;

    return n;
}

uint32_t str2uint32_t(const char *s) {
    uint32_t n = 0;
    char     c;
    for (c = *s; c >= '0' && c <= '9'; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }
    return n;
}

uint64_t str2uint64_t(const char *s) {
    uint64_t n = 0;
    char     c;
    for (c = *s; c >= '0' && c <= '9'; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }
    return n;
}

unsigned long str2ul(const char *s) {
    unsigned long n = 0;
    char          c;
    for (c = *s; c >= '0' && c <= '9'; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }
    return n;
}

unsigned long long str2ull(const char *s) {
    unsigned long long n = 0;
    char               c;
    for (c = *s; c >= '0' && c <= '9'; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }
    return n;
}

int64_t str2int64_t(const char *s, char **endptr) {
    int32_t negative = 0;

    if (unlikely(*s == '-')) {
        s++;
        negative = 1;
    } else if (unlikely(*s == '+'))
        s++;

    int64_t n = 0;
    char    c;
    for (c = *s; c >= '0' && c <= '9'; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }

    if (unlikely(endptr))
        *endptr = (char *)s;

    if (unlikely(negative))
        return -n;
    else
        return n;
}

long double str2ld(const char *s, char **endptr) {
    int                negative = 0;
    const char        *start = s;
    unsigned long long integer_part = 0;
    unsigned long      decimal_part = 0;
    size_t             decimal_digits = 0;

    switch (*s) {
    case '-':
        s++;
        negative = 1;
        break;

    case '+':
        s++;
        break;

    case 'n':
        if (s[1] == 'a' && s[2] == 'n') {
            if (endptr)
                *endptr = (char *)&s[3];
            return NAN;
        }
        break;

    case 'i':
        if (s[1] == 'n' && s[2] == 'f') {
            if (endptr)
                *endptr = (char *)&s[3];
            return INFINITY;
        }
        break;

    default:
        break;
    }

    while (*s >= '0' && *s <= '9') {
        integer_part = (integer_part * 10) + (*s - '0');
        s++;
    }

    if (unlikely(*s == '.')) {
        decimal_part = 0;
        s++;

        while (*s >= '0' && *s <= '9') {
            decimal_part = (decimal_part * 10) + (*s - '0');
            s++;
            decimal_digits++;
        }
    }

    if (unlikely(*s == 'e' || *s == 'E'))
        return strtold(start, endptr);

    if (unlikely(endptr))
        *endptr = (char *)s;

    if (unlikely(negative)) {
        if (unlikely(decimal_digits))
            return -((long double)integer_part
                     + (long double)decimal_part / powl(10.0, decimal_digits));
        else
            return -((long double)integer_part);
    } else {
        if (unlikely(decimal_digits))
            return (long double)integer_part
                   + (long double)decimal_part / powl(10.0, decimal_digits);
        else
            return (long double)integer_part;
    }
}

char *itoa(int32_t value, char *result, int32_t base) {
    // check that the base if valid
    if (base < 2 || base > 36) {
        *result = '\0';
        return result;
    }

    char   *ptr = result, *ptr1 = result, tmp_char;
    int32_t tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"
            [35 + (tmp_value - value * base)];
    } while (value);

    // Apply negative sign
    if (tmp_value < 0)
        *ptr++ = '-';
    *ptr-- = '\0';
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

uint32_t bkrd_hash(const void *key, size_t len) {
    uint8_t *p_key = NULL;
    uint8_t *p_end = NULL;
    uint32_t seed = 131;   //  31 131 1313 13131 131313 etc..
    uint32_t hash = 0;

    p_end = (uint8_t *)key + len;
    for (p_key = (uint8_t *)key; p_key != p_end; p_key++) {
        hash = hash * seed + (*p_key);
    }

    return hash;
}

uint32_t simple_hash(const char *name) {
    unsigned char *s = (unsigned char *)name;
    uint32_t       hval = 0x811c9dc5;
    while (*s) {
        hval *= 16777619;
        hval ^= (uint32_t)*s++;
    }
    return hval;
}

/**
 * Copy the source string to the destination string, but don't copy more than siz-1 characters, and
 * always make sure that the destination string is null terminated
 *
 * @param dst The destination string.
 * @param src The source string to copy.
 * @param siz The size of the destination buffer.
 *
 * @return The number of characters of src.
 */
size_t strlcpy(char *dst, const char *src, size_t siz) {
    char       *d = dst;
    const char *s = src;
    size_t      n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0')
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            // dst空间不足，在最后加上'\0'
            *d = '\0'; /* NUL-terminate dst */
        while (*s++)
            ;
    }

    return (s - src - 1); /* count does not include NUL */
}