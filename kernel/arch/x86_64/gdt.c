/* gdt.c - eigene GDT mit TSS; der Doppelfehler bekommt einen eigenen Stapel (IST1). */
#include "arch.h"
#include "x86.h"

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3, iomap;
} __attribute__((packed));

static struct tss tss;
static uint64_t gdt[5];
static uint8_t df_stack[4096] __attribute__((aligned(16)));

void gdt_init(void)
{
    uint64_t base = (uint64_t)&tss, limit = sizeof tss - 1;
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFF;   /* Kernel-Code, 64 Bit */
    gdt[2] = 0x00CF92000000FFFF;   /* Kernel-Daten */
    gdt[3] = (limit & 0xFFFF) | (base & 0xFFFFFF) << 16 | 0x89ull << 40 |
             ((limit >> 16) & 0xF) << 48 | ((base >> 24) & 0xFF) << 56;
    gdt[4] = base >> 32;
    tss.ist[0] = (uint64_t)(df_stack + sizeof df_stack);
    tss.iomap = sizeof tss;

    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr = { sizeof gdt - 1, (uint64_t)gdt };

    __asm__ volatile(
        "lgdt %0\n"
        "pushq %1\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw %w2, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "ltr %w3\n"
        :
        : "m"(gdtr), "i"(GDT_KCODE), "r"(GDT_KDATA), "r"(GDT_TSS)
        : "rax", "memory");
}
