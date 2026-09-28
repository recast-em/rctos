/* log.c - Kernel-Meldungen: jede Zeile auf die serielle Schnittstelle (ASCII),
 * und sobald die Konsole steht auch in ihren Meldungsbereich. */
#include "arch.h"
#include "cellcon.h"
#include "log.h"

static int area_top = -1, area_bottom, area_row;
static uint8_t area_attr;

void klog_area(int top, int bottom, uint8_t attr)
{
    area_top = area_row = top;
    area_bottom = bottom;
    area_attr = attr;
}

void klog_line(const char *line)
{
    arch_serial_write(line, strlen(line));
    arch_serial_write("\r\n", 2);
    if (area_top < 0 || !con_ready())
        return;
    if (area_row >= area_bottom) {
        con_scroll(area_top, area_bottom, area_attr);
        area_row = area_bottom - 1;
    }
    con_text(2, area_row++, area_attr, line);
}

void klog(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    rc_vfmt(buf, sizeof buf, fmt, ap);
    va_end(ap);
    klog_line(buf);
}
