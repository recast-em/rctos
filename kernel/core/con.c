/* con.c - Zellenkonsole: jede Zelle wird genau einmal gemalt, direkt in den
 * Framebuffer. Kein Rückspeicher: der Bildschirm ist der Speicher. */
#include "con.h"

#include "font.h"

/* 0xRRGGBB; Eintrag 1 ist das Recaster-Blau #2449AA (RGB332 0x2A der Familie). */
static const uint32_t palette[16] = {
    0x000000, 0x2449AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static struct rc_fb fb;
static bool ready;
static int cols, rows, scale, cell_w, cell_h, x0, y0;
static uint32_t pixel[16];

static uint32_t encode(uint32_t rgb)
{
    uint32_t r = rgb >> 16 & 0xFF, g = rgb >> 8 & 0xFF, b = rgb & 0xFF;
    return (r >> (8 - fb.red_size)) << fb.red_shift |
           (g >> (8 - fb.green_size)) << fb.green_shift |
           (b >> (8 - fb.blue_size)) << fb.blue_shift;
}

bool con_init(const struct rc_fb *f)
{
    if (f->bpp != 32 || !f->base || f->red_size > 8 || f->green_size > 8 || f->blue_size > 8)
        return false;
    fb = *f;
    /* Das Raster bleibt logisch 8 x 8; große Bildschirme zeichnen jede Zelle vergrößert. */
    scale = fb.width >= 960 ? (int)(fb.width / 960) : 1;
    cell_w = 8 * scale;
    cell_h = 8 * scale;
    cols = (int)(fb.width / cell_w);
    rows = (int)(fb.height / cell_h);
    x0 = (int)(fb.width - cols * cell_w) / 2;
    y0 = (int)(fb.height - rows * cell_h) / 2;
    for (int i = 0; i < 16; i++)
        pixel[i] = encode(palette[i]);
    for (uint32_t y = 0; y < fb.height; y++) {
        uint32_t *line = (uint32_t *)(fb.base + (uint64_t)y * fb.pitch);
        for (uint32_t x = 0; x < fb.width; x++)
            line[x] = pixel[RC_BLACK];
    }
    ready = true;
    return true;
}

bool con_ready(void)
{
    return ready;
}

int con_cols(void)
{
    return cols;
}

int con_rows(void)
{
    return rows;
}

int con_scale(void)
{
    return scale;
}

void con_put(int col, int row, uint8_t ch, uint8_t attr)
{
    if (!ready || col < 0 || row < 0 || col >= cols || row >= rows)
        return;
    uint32_t fg = pixel[attr & 15], bg = pixel[attr >> 4];
    const uint8_t *glyph = rc_font8x8[ch];
    uint8_t *line = fb.base + (uint64_t)(y0 + row * cell_h) * fb.pitch +
                    (uint64_t)(x0 + col * cell_w) * 4;
    for (int y = 0; y < 8; y++) {
        for (int sy = 0; sy < scale; sy++, line += fb.pitch) {
            uint32_t *px = (uint32_t *)line;
            for (int x = 0; x < 8; x++) {
                uint32_t c = glyph[y] & (0x80 >> x) ? fg : bg;
                for (int sx = 0; sx < scale; sx++)
                    *px++ = c;
            }
        }
    }
}

int con_text(int col, int row, uint8_t attr, const char *s)
{
    for (; *s; s++, col++)
        con_put(col, row, (uint8_t)*s, attr);
    return col;
}

void con_fill(int col, int row, int w, int h, uint8_t ch, uint8_t attr)
{
    for (int r = row; r < row + h; r++)
        for (int c = col; c < col + w; c++)
            con_put(c, r, ch, attr);
}

/* Zeilen [top, bottom) um eine Zelle nach oben; die letzte wird leer. */
void con_scroll(int top, int bottom, uint8_t attr)
{
    if (!ready || top < 0 || bottom > rows || bottom - top < 2)
        return;
    uint64_t line = (uint64_t)fb.pitch;
    uint64_t *dst = (uint64_t *)(fb.base + (uint64_t)(y0 + top * cell_h) * line);
    const uint64_t *src = (const uint64_t *)((uint8_t *)dst + cell_h * line);
    uint64_t words = (uint64_t)(bottom - top - 1) * cell_h * line / 8;
    while (words--)
        *dst++ = *src++;
    con_fill(0, bottom - 1, cols, 1, ' ', attr);
}
