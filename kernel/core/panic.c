/* panic.c - wenn der Kernel nicht weiter kann: alles zeigen, dann anhalten.
 * Ein Fehler ist ein Exponat, kein stilles Ende. */
#include "arch.h"
#include "cellcon.h"

static void serial_text(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            arch_serial_write("\r", 1);
        arch_serial_write(s, 1);
    }
}

_Noreturn void rc_panic(const char *title, const char *body)
{
    arch_irq_disable();
    serial_text("\nPANIC: ");
    serial_text(title);
    serial_text("\n");
    serial_text(body);
    serial_text("\n");
    if (con_ready()) {
        int cols = con_cols(), rows = con_rows(), top = rows / 2;
        uint8_t text = RC_ATTR(RC_WHITE, RC_BLACK), bar = RC_ATTR(RC_WHITE, RC_RED);
        con_fill(0, top, cols, rows - top, ' ', text);
        con_fill(0, top, cols, 1, ' ', bar);
        con_text(con_text(2, top, bar, "PANIC  "), top, bar, title);
        int row = top + 2, col = 2;
        for (const char *s = body; *s && row < rows - 2; s++) {
            if (*s == '\n') {
                row++;
                col = 2;
            } else {
                con_put(col++, row, (uint8_t)*s, text);
            }
        }
        con_fill(0, rows - 1, cols, 1, ' ', bar);
        con_text(2, rows - 1, bar, "the kernel stopped here - this screen is the exhibit");
    }
    arch_halt_forever();
}
