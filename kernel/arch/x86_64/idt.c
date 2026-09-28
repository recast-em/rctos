/* idt.c - Ausnahmetabelle: Ausnahmen werden zur Panik mit allen Registern,
 * der Zeitgeber setzt nur ein Signal für die Leerlaufschleife. */
#include "arch.h"
#include "x86.h"

struct x86_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};

struct idt_entry {
    uint16_t offset_lo, selector;
    uint8_t ist, type;
    uint16_t offset_mid;
    uint32_t offset_hi, zero;
} __attribute__((packed));

extern const uint64_t isr_exceptions[32];
extern char isr48[], isr255[];

static struct idt_entry idt[256];

static const char *const names[32] = {
    "divide error", "debug", "nmi", "breakpoint", "overflow", "bound range",
    "invalid opcode", "device not available", "double fault", "coprocessor overrun",
    "invalid tss", "segment not present", "stack fault", "general protection",
    "page fault", "reserved", "x87 error", "alignment check", "machine check",
    "simd error", "virtualization", "control protection", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved", "hypervisor injection",
    "vmm communication", "security", "reserved",
};

static void set_gate(int vec, uint64_t handler, uint8_t ist)
{
    idt[vec] = (struct idt_entry){
        .offset_lo = (uint16_t)handler,
        .selector = GDT_KCODE,
        .ist = ist,
        .type = 0x8E,   /* vorhanden, Ring 0, Interrupt-Gate */
        .offset_mid = (uint16_t)(handler >> 16),
        .offset_hi = (uint32_t)(handler >> 32),
    };
}

void idt_load(void)
{
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr = { sizeof idt - 1, (uint64_t)idt };
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void idt_init(void)
{
    for (int v = 0; v < 32; v++)
        set_gate(v, isr_exceptions[v], v == 8 ? 1 : 0);
    set_gate(VEC_TIMER, (uint64_t)isr48, 0);
    set_gate(VEC_SPURIOUS, (uint64_t)isr255, 0);
    idt_load();
}

void x86_trap(struct x86_frame *f);
void x86_trap(struct x86_frame *f)
{
    if (f->vector == VEC_TIMER) {
        arch_timer_fired = 1;
        lapic_eoi();
        return;
    }
    if (f->vector == VEC_SPURIOUS)
        return;

    char title[64], body[640];
    rc_fmt(title, sizeof title, "%s (vector %u)",
           f->vector < 32 ? names[f->vector] : "unexpected interrupt", (unsigned)f->vector);
    rc_fmt(body, sizeof body,
           "rip %p  cs %02x  rflags %08x  error %x  cr2 %p\n"
           "rax %p  rbx %p  rcx %p  rdx %p\n"
           "rsi %p  rdi %p  rbp %p  rsp %p\n"
           "r8  %p  r9  %p  r10 %p  r11 %p\n"
           "r12 %p  r13 %p  r14 %p  r15 %p",
           (void *)f->rip, (unsigned)f->cs, (unsigned)f->rflags, (unsigned)f->error,
           (void *)read_cr2(), (void *)f->rax, (void *)f->rbx, (void *)f->rcx,
           (void *)f->rdx, (void *)f->rsi, (void *)f->rdi, (void *)f->rbp, (void *)f->rsp,
           (void *)f->r8, (void *)f->r9, (void *)f->r10, (void *)f->r11, (void *)f->r12,
           (void *)f->r13, (void *)f->r14, (void *)f->r15);
    rc_panic(title, body);
}
