/* fmt.c - formatierte Ausgabe: %c %s %d %i %u %x %X %p, Breite, '0', '-',
 * "'" (Tausender-Trennung) und die Längen l, ll, z. Mehr braucht der Kernel nicht. */
#include "kernel.h"

struct out {
    char *buf;
    size_t cap, len;
};

static void put(struct out *o, char c)
{
    if (o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void pad(struct out *o, char c, int n)
{
    while (n-- > 0)
        put(o, c);
}

static void number(struct out *o, uint64_t v, bool neg, unsigned base, bool upper,
                   int width, bool zero, bool left, bool group)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int n = 0, count = 0;
    do {
        if (group && count && count % 3 == 0)
            tmp[n++] = ',';
        tmp[n++] = digits[v % base];
        v /= base;
        count++;
    } while (v);
    if (neg)
        tmp[n++] = '-';
    int fill = width - n;
    if (!left)
        pad(o, zero ? '0' : ' ', fill);
    while (n)
        put(o, tmp[--n]);
    if (left)
        pad(o, ' ', fill);
}

size_t rc_vfmt(char *buf, size_t cap, const char *fmt, va_list ap)
{
    struct out o = { buf, cap, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        bool zero = false, left = false, group = false;
        int width = 0, lng = 0;
        for (;; fmt++) {
            if (fmt[1] == '0')
                zero = true;
            else if (fmt[1] == '-')
                left = true;
            else if (fmt[1] == '\'')
                group = true;
            else
                break;
        }
        while (fmt[1] >= '0' && fmt[1] <= '9')
            width = width * 10 + (*++fmt - '0');
        while (fmt[1] == 'l' || fmt[1] == 'z') {
            fmt++;
            lng++;
        }
        char c = *++fmt;
        switch (c) {
        case 'c':
            pad(&o, ' ', left ? 0 : width - 1);
            put(&o, (char)va_arg(ap, int));
            pad(&o, ' ', left ? width - 1 : 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int n = (int)strlen(s);
            if (!left)
                pad(&o, ' ', width - n);
            while (*s)
                put(&o, *s++);
            if (left)
                pad(&o, ' ', width - n);
            break;
        }
        case 'd':
        case 'i': {
            int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            uint64_t u = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
            number(&o, u, v < 0, 10, false, width, zero, left, group);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            number(&o, v, false, c == 'u' ? 10 : 16, c == 'X', width, zero, left,
                   group && c == 'u');
            break;
        }
        case 'p':
            put(&o, '0');
            put(&o, 'x');
            number(&o, (uint64_t)(uintptr_t)va_arg(ap, void *), false, 16, false, 16,
                   true, false, false);
            break;
        case '%':
            put(&o, '%');
            break;
        default:
            put(&o, '%');
            if (c)
                put(&o, c);
            else
                fmt--;
            break;
        }
    }
    if (cap)
        buf[o.len < cap ? o.len : cap - 1] = '\0';
    return o.len;
}

size_t rc_fmt(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t n = rc_vfmt(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

const char *rc_fmt_size(char *buf, size_t cap, uint64_t bytes)
{
    if (bytes < RC_KIB(10))
        rc_fmt(buf, cap, "%u B", (unsigned)bytes);
    else if (bytes < RC_MIB(10))
        rc_fmt(buf, cap, "%u KiB", (unsigned)(bytes >> 10));
    else if (bytes < RC_MIB(10240))
        rc_fmt(buf, cap, "%u MiB", (unsigned)(bytes >> 20));
    else
        rc_fmt(buf, cap, "%u.%u GiB", (unsigned)(bytes >> 30),
               (unsigned)(((bytes >> 20) & 1023) * 10 / 1024));
    return buf;
}
