#ifndef MICRO_OS_APP_UTILS_H
#define MICRO_OS_APP_UTILS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

static inline bool str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == '\0' && *b == '\0';
}
static inline bool str_prefix(const char *s, const char *p)
{
    while (*p) { if (*s++ != *p++) return false; }
    return true;
}
static inline void str_copy(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    while (i + 1u < cap && src[i]) { dst[i] = src[i]; ++i; }
    if (cap) dst[i] = '\0';
}
static inline size_t str_length(const char *src)
{
    size_t n = 0;
    while (src[n] != '\0') ++n;
    return n;
}
static inline void str_append(char *dst, size_t cap, const char *src)
{
    size_t at = str_length(dst);
    while (at + 1u < cap && *src) dst[at++] = *src++;
    if (cap) dst[at] = '\0';
}
static inline void append_u64(char *dst, size_t cap, uint64_t x)
{
    char digits[21]; unsigned n = 0;
    do { digits[n++] = (char)('0' + (x % 10u)); x /= 10u; } while (x);
    char c[2] = {0,0};
    while (n) { c[0] = digits[--n]; str_append(dst,cap,c); }
}
static inline void append_i64(char *dst, size_t cap, int64_t x)
{
    if (x < 0) { str_append(dst, cap, "-"); append_u64(dst,cap,(uint64_t)(-(x+1))+1u); }
    else append_u64(dst,cap,(uint64_t)x);
}
static inline void skip_spaces(const char **p)
{
    while (**p == ' ') ++*p;
}
/* Application inputs deliberately limited to +-1,000,000 so arithmetic is
 * bounded and cannot overflow signed 64-bit during multiplication. */
static inline bool parse_int(const char **s, int64_t *value)
{
    const char *p = *s;
    skip_spaces(&p);
    int sign = 1;
    if (*p == '-') { sign = -1; ++p; }
    else if (*p == '+') ++p;
    if (*p < '0' || *p > '9') return false;
    int64_t n = 0;
    while (*p >= '0' && *p <= '9') {
        n = n*10 + (*p++ - '0');
        if (n > 1000000) return false;
    }
    *value = n*sign;
    *s = p;
    return true;
}
#endif
