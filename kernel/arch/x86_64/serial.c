/* serial.c - 16550-UART an COM1 als frühe Ausgabe (115200 Baud, 8N1). */
#include "arch.h"
#include "x86.h"

#define COM1 0x3F8

static bool present;

void arch_early_init(void)
{
    outb(COM1 + 1, 0x00);   /* keine UART-Interrupts */
    outb(COM1 + 3, 0x80);   /* Teiler setzen ... */
    outb(COM1 + 0, 0x01);   /* ... 115200 Baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 Bit, keine Parität, 1 Stoppbit */
    outb(COM1 + 2, 0xC7);   /* FIFO an und leeren */
    outb(COM1 + 4, 0x03);   /* DTR, RTS */
    outb(COM1 + 7, 0xAE);   /* Scratch-Register: gibt es die UART überhaupt? */
    present = inb(COM1 + 7) == 0xAE;
}

void arch_serial_write(const char *s, size_t n)
{
    if (!present)
        return;
    for (; n; n--, s++) {
        for (int spin = 100000; !(inb(COM1 + 5) & 0x20) && spin; spin--)
            ;
        outb(COM1, (uint8_t)*s);
    }
}
