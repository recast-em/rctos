/* lapic.c - Local APIC als Zeitgeber im Einmal-Modus. Kalibriert wird gegen den
 * TSC, dessen Frequenz der Bootloader mitteilt: kein PIT, keine Warteschleife. */
#include "arch.h"
#include "x86.h"

#define MSR_APIC_BASE 0x1B
#define REG_EOI 0x0B0
#define REG_SVR 0x0F0
#define REG_LVT_TIMER 0x320
#define REG_TIMER_INIT 0x380
#define REG_TIMER_CUR 0x390
#define REG_TIMER_DIV 0x3E0
#define LVT_MASKED (1u << 16)

volatile uint32_t arch_timer_fired;

static volatile uint32_t *lapic;
static uint64_t timer_hz;

static uint32_t rd(uint32_t reg)
{
    return lapic[reg / 4];
}

static void wr(uint32_t reg, uint32_t v)
{
    lapic[reg / 4] = v;
}

void lapic_eoi(void)
{
    wr(REG_EOI, 0);
}

bool arch_timer_init(uint64_t counter_hz)
{
    uint64_t base = rdmsr(MSR_APIC_BASE);
    if (!(base & (1u << 11)))
        return false;   /* Local APIC abgeschaltet */
    lapic = arch_map_mmio(base & 0xFFFFFFFFFF000ull, 4096);
    wr(REG_SVR, 0x100 | VEC_SPURIOUS);
    wr(REG_TIMER_DIV, 0x3);   /* Teiler 16 */

    /* 5 ms messen: wie weit zählt der APIC-Zeitgeber in dieser Zeit herunter? */
    wr(REG_LVT_TIMER, LVT_MASKED | VEC_TIMER);
    wr(REG_TIMER_INIT, 0xFFFFFFFF);
    uint64_t start = rdtsc(), span = counter_hz / 200;
    while (rdtsc() - start < span)
        ;
    uint32_t ticks = 0xFFFFFFFF - rd(REG_TIMER_CUR);
    wr(REG_TIMER_INIT, 0);
    timer_hz = (uint64_t)ticks * 200;
    if (!timer_hz)
        return false;
    wr(REG_LVT_TIMER, VEC_TIMER);   /* Einmal-Modus, nicht maskiert */
    return true;
}

uint64_t arch_timer_hz(void)
{
    return timer_hz;
}

/* Längere Zeiten als der Zähler fasst werden gekürzt; die Schleife stellt nach. */
void arch_timer_arm_ns(uint64_t ns)
{
    uint64_t count = ns / 1000 * (timer_hz / 1000) / 1000;
    if (count > 0xFFFFFFFF)
        count = 0xFFFFFFFF;
    wr(REG_TIMER_INIT, count ? (uint32_t)count : 1);
}
