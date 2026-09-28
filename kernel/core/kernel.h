/* kernel.h - gemeinsame Grundlagen des Kernels (architekturunabhängig). */
#ifndef RC_KERNEL_H
#define RC_KERNEL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RC_VERSION "0.0.1"
#define RC_MILESTONE "m1"

#define RC_KIB(n) ((uint64_t)(n) << 10)
#define RC_MIB(n) ((uint64_t)(n) << 20)
#define RC_PAGE_SIZE 4096u
#define RC_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define RC_STR_(x) #x
#define RC_STR(x) RC_STR_(x)
#define RC_ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~(uint64_t)((a) - 1))
#define RC_MIN(a, b) ((a) < (b) ? (a) : (b))
#define RC_MAX(a, b) ((a) > (b) ? (a) : (b))

/* lib.c - was der Compiler auch ohne libc voraussetzt. */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

/* fmt.c - formatierte Ausgabe in einen Puffer (Teilmenge von printf). */
size_t rc_vfmt(char *buf, size_t cap, const char *fmt, va_list ap);
size_t rc_fmt(char *buf, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
/* Größe in Bytes als "37 KiB", "1015 MiB", "1.9 GiB". */
const char *rc_fmt_size(char *buf, size_t cap, uint64_t bytes);

/* log.c - eine Zeile auf die serielle Schnittstelle (und den Bildschirm). */
void klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* panic.c - Fehler sind Exponate: sichtbar machen, anhalten. */
_Noreturn void rc_panic(const char *title, const char *body);

#endif
