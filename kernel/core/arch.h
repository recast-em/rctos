/* arch.h - was der Kern von der Architekturschicht erwartet.
 * Umgesetzt in kernel/arch/<arch>/; Assembler gibt es nur dort. */
#ifndef RC_ARCH_H
#define RC_ARCH_H

#include "kernel.h"

struct limine_mp_info;

const char *arch_name(void);

/* Frühe Ausgabe: serielle Schnittstelle. */
void arch_early_init(void);
void arch_serial_write(const char *s, size_t n);

/* Deskriptortabellen und Ausnahmen; danach sind Fehler sichtbar. */
void arch_init(void);

/* Frei laufender Zähler (x86-64: TSC) und Interrupt-Steuerung. */
uint64_t arch_counter(void);
void arch_irq_disable(void);
void arch_irq_enable(void);
void arch_wait(void);              /* Interrupts an und schlafen, atomar */
_Noreturn void arch_halt_forever(void);
void arch_ap_park(struct limine_mp_info *info);

/* Eine MMIO-Region ungecacht in die Direktabbildung eintragen. */
void *arch_map_mmio(uint64_t phys, uint64_t size);

/* Zeitgeber im Einmal-Modus (x86-64: Local APIC), kalibriert gegen den Zähler. */
bool arch_timer_init(uint64_t counter_hz);
uint64_t arch_timer_hz(void);
void arch_timer_arm_ns(uint64_t ns);
extern volatile uint32_t arch_timer_fired;

/* CPU-Beschreibung für den Boot-Bericht. */
struct rc_cpu {
    char vendor[13];
    char brand[49];
    bool baseline;        /* x86-64: x86-64-v2 */
    bool nx, x2apic, tsc_deadline, invariant_tsc, hypervisor;
};
void arch_cpu_describe(struct rc_cpu *out);

#endif
