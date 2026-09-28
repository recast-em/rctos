/* cpu.c - CPU-Beschreibung, Zähler, Interrupt-Steuerung, Schlafen. */
#include "arch.h"
#include "x86.h"

const char *arch_name(void)
{
    return "x86_64";
}

void arch_init(void)
{
    gdt_init();
    idt_init();
}

uint64_t arch_counter(void)
{
    return rdtsc();
}

void arch_irq_disable(void)
{
    __asm__ volatile("cli" ::: "memory");
}

void arch_irq_enable(void)
{
    __asm__ volatile("sti" ::: "memory");
}

/* sti wirkt erst nach dem nächsten Befehl: zwischen sti und hlt geht kein Interrupt verloren. */
void arch_wait(void)
{
    __asm__ volatile("sti; hlt" ::: "memory");
}

_Noreturn void arch_halt_forever(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

/* Weitere CPUs: gültige Ausnahmetabelle laden, dann für immer schlafen. */
void arch_ap_park(struct limine_mp_info *info)
{
    (void)info;
    idt_load();
    arch_halt_forever();
}

static void copy_regs(char *dst, const uint32_t *regs, int n)
{
    memcpy(dst, regs, (size_t)n * 4);
}

void arch_cpu_describe(struct rc_cpu *out)
{
    uint32_t r[4], max, ext;
    memset(out, 0, sizeof *out);

    cpuid(0, 0, r);
    max = r[0];
    copy_regs(out->vendor, (uint32_t[]){ r[1], r[3], r[2] }, 3);

    cpuid(0x80000000, 0, r);
    ext = r[0];
    if (ext >= 0x80000004) {
        for (uint32_t i = 0; i < 3; i++) {
            cpuid(0x80000002 + i, 0, r);
            copy_regs(out->brand + i * 16, r, 4);
        }
        /* Führende Leerzeichen entfernen. */
        char *b = out->brand;
        while (*b == ' ')
            b++;
        memmove(out->brand, b, strlen(b) + 1);
    }

    uint32_t ecx1 = 0, ecx81 = 0, edx81 = 0;
    if (max >= 1) {
        cpuid(1, 0, r);
        ecx1 = r[2];
    }
    if (ext >= 0x80000001) {
        cpuid(0x80000001, 0, r);
        ecx81 = r[2];
        edx81 = r[3];
    }
    if (ext >= 0x80000007) {
        cpuid(0x80000007, 0, r);
        out->invariant_tsc = r[3] >> 8 & 1;
    }
    /* x86-64-v2: SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B, LAHF/SAHF. */
    uint32_t v2 = 1u << 0 | 1u << 9 | 1u << 13 | 1u << 19 | 1u << 20 | 1u << 23;
    out->baseline = (ecx1 & v2) == v2 && (ecx81 & 1);
    out->nx = edx81 >> 20 & 1;
    out->x2apic = ecx1 >> 21 & 1;
    out->tsc_deadline = ecx1 >> 24 & 1;
    out->hypervisor = ecx1 >> 31 & 1;
}
