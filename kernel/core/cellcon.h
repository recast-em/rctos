/* cellcon.h - Zellenkonsole: Textzellen (CP437, 8 x 8) direkt auf dem Framebuffer.
 * Ohne Zwischenspeicher: der Bildschirm ist der Speicher. */
#ifndef RC_CELLCON_H
#define RC_CELLCON_H

#include "kernel.h"

struct rc_fb {
    uint8_t *base;
    uint32_t width, height, pitch, bpp;
    uint8_t red_shift, red_size, green_shift, green_size, blue_shift, blue_size;
};

/* Palette 0: die EGA-Farben der Familie, Eintrag 1 ist das Recaster-Blau. */
enum {
    RC_BLACK, RC_BLUE, RC_GREEN, RC_CYAN, RC_RED, RC_MAGENTA, RC_BROWN, RC_LGRAY,
    RC_DGRAY, RC_LBLUE, RC_LGREEN, RC_LCYAN, RC_LRED, RC_LMAGENTA, RC_YELLOW, RC_WHITE,
};
#define RC_ATTR(fg, bg) ((uint8_t)((fg) | (bg) << 4))

bool con_init(const struct rc_fb *fb, bool doubled);
bool con_ready(void);
int con_cols(void);
int con_rows(void);
int con_scale(void);
void con_put(int col, int row, uint8_t ch, uint8_t attr);
int con_text(int col, int row, uint8_t attr, const char *s);
void con_fill(int col, int row, int w, int h, uint8_t ch, uint8_t attr);
void con_scroll(int top, int bottom, uint8_t attr);

#endif
