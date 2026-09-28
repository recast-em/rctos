/* paging.c - eigene Seitentabellen: die Direktabbildung (an derselben Stelle wie beim
 * Bootloader, damit alle Zeiger gültig bleiben), der Kernel mit getrennten Rechten je
 * Segment (W^X) und MMIO-Seiten nach Bedarf. Tabellen kommen aus pmm_alloc. */
#include "arch.h"
#include "boot.h"
#include "pmm.h"
#include "x86.h"

#define PTE_P (1ull << 0)
#define PTE_W (1ull << 1)
#define PTE_PWT (1ull << 3)
#define PTE_PCD (1ull << 4)
#define PTE_PS (1ull << 7)
#define PTE_PAT4K (1ull << 7)
#define PTE_PATBIG (1ull << 12)
#define PTE_NX (1ull << 63)
#define PTE_ADDR 0x000FFFFFFFFFF000ull
#define MSR_EFER 0xC0000080
#define MSR_PAT 0x277

/* PAT wie bei Limine: 0 WB, 1 WT, 2 UC-, 3 UC, 4 WP, 5 WC, 6 UC-, 7 UC. */
#define PAT_VALUE 0x0007010500070406ull

enum cache { CACHE_WB, CACHE_WC, CACHE_UC };

extern char rc_image_start[], rc_text_start[], rc_text_end[], rc_rodata_start[],
    rc_rodata_end[], rc_data_start[], rc_bss_end[];

static uint64_t nx, pml4_phys;
static bool huge1g;
static uint32_t tables;

static uint64_t *table(uint64_t phys)
{
    return (uint64_t *)(boot_hhdm() + (phys & PTE_ADDR));
}

static uint64_t *descend(uint64_t *entry)
{
    if (!(*entry & PTE_P)) {
        uint64_t page = pmm_alloc();
        if (!page)
            rc_panic("paging", "no memory for a page table");
        tables++;
        *entry = page | PTE_P | PTE_W;
    } else if (*entry & PTE_PS) {
        rc_panic("paging", "a large page already covers this address");
    }
    return table(*entry);
}

static uint64_t leaf_bits(bool write, bool exec, enum cache c, bool big)
{
    uint64_t f = PTE_P | (write ? PTE_W : 0) | (exec ? 0 : nx);
    if (c == CACHE_WC)
        f |= PTE_PWT | (big ? PTE_PATBIG : PTE_PAT4K);   /* PAT-Index 5 */
    else if (c == CACHE_UC)
        f |= PTE_PWT | PTE_PCD;                         /* PAT-Index 3 */
    return f;
}

/* [va, va + len) auf pa abbilden, mit den größten passenden Seiten. */
static void map(uint64_t va, uint64_t pa, uint64_t len, bool write, bool exec, enum cache c)
{
    while (len) {
        uint64_t *pdpt = descend(&table(pml4_phys)[va >> 39 & 511]);
        if (huge1g && !((va | pa) & 0x3FFFFFFF) && len >= (1ull << 30)) {
            pdpt[va >> 30 & 511] = pa | PTE_PS | leaf_bits(write, exec, c, true);
            va += 1ull << 30, pa += 1ull << 30, len -= 1ull << 30;
            continue;
        }
        uint64_t *pd = descend(&pdpt[va >> 30 & 511]);
        if (!((va | pa) & 0x1FFFFF) && len >= (1ull << 21)) {
            pd[va >> 21 & 511] = pa | PTE_PS | leaf_bits(write, exec, c, true);
            va += 1ull << 21, pa += 1ull << 21, len -= 1ull << 21;
            continue;
        }
        uint64_t *pt = descend(&pd[va >> 21 & 511]);
        pt[va >> 12 & 511] = pa | leaf_bits(write, exec, c, false);
        va += RC_PAGE_SIZE, pa += RC_PAGE_SIZE, len -= RC_PAGE_SIZE;
    }
}

static void map_kernel(uint64_t kphys, uint64_t kvirt, const char *start, const char *end,
                       bool write, bool exec)
{
    uint64_t va = (uint64_t)start, len = RC_ALIGN_UP((uint64_t)(end - start), RC_PAGE_SIZE);
    map(va, kphys + (va - kvirt), len, write, exec, CACHE_WB);
}

void arch_vm_init(uint64_t kernel_phys, uint64_t kernel_virt)
{
    uint32_t r[4];
    cpuid(0x80000001, 0, r);
    huge1g = r[3] >> 26 & 1;
    nx = rdmsr(MSR_EFER) & (1u << 11) ? PTE_NX : 0;
    if (read_cr4() & (1u << 12))
        rc_panic("paging", "5-level paging is not supported yet");
    wrmsr(MSR_PAT, PAT_VALUE);

    pml4_phys = pmm_alloc();
    tables = 1;

    /* Direktabbildung: alles, was RAM, Firmware-Daten oder Framebuffer ist. Aneinander
     * grenzende Bereiche gleicher Art werden zusammengefasst, damit große Seiten passen. */
    uint64_t base, len, run_b = 0, run_e = 0;
    enum rc_memtype type;
    enum cache run_c = CACHE_WB;
    for (uint32_t i = 0;; i++) {
        bool more = boot_memmap(i, &base, &len, &type);
        if (more && type == RC_MEM_UNMAPPED)
            continue;
        uint64_t b = base & ~(uint64_t)4095, e = RC_ALIGN_UP(base + len, RC_PAGE_SIZE);
        enum cache c = type == RC_MEM_FRAMEBUFFER ? CACHE_WC : CACHE_WB;
        if (more && run_e > run_b && b <= run_e && c == run_c) {
            run_e = RC_MAX(run_e, e);
            continue;
        }
        if (run_e > run_b)
            map(boot_hhdm() + run_b, run_b, run_e - run_b, true, false, run_c);
        if (!more)
            break;
        run_b = b, run_e = e, run_c = c;
    }

    /* Der Kernel: jedes Segment mit genau seinen Rechten. */
    map_kernel(kernel_phys, kernel_virt, rc_image_start, rc_text_start, true, false);
    map_kernel(kernel_phys, kernel_virt, rc_text_start, rc_text_end, false, true);
    map_kernel(kernel_phys, kernel_virt, rc_rodata_start, rc_rodata_end, false, false);
    map_kernel(kernel_phys, kernel_virt, rc_data_start, rc_bss_end, true, false);

    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

void *arch_map_mmio(uint64_t phys, uint64_t size)
{
    uint64_t start = phys & PTE_ADDR, end = RC_ALIGN_UP(phys + size, RC_PAGE_SIZE);
    for (uint64_t p = start; p < end; p += RC_PAGE_SIZE) {
        map(boot_hhdm() + p, p, RC_PAGE_SIZE, true, false, CACHE_UC);
        invlpg((void *)(boot_hhdm() + p));
    }
    return (void *)(boot_hhdm() + phys);
}

/* Welche Rechte hat die Adresse? Für den Selbsttest und später /now. */
uint32_t arch_vm_query(uint64_t va)
{
    uint64_t e = table(read_cr3())[va >> 39 & 511];
    int shift = 39;
    while ((e & PTE_P) && shift > 12 && !((e & PTE_PS) && shift < 39)) {
        shift -= 9;
        e = table(e)[va >> shift & 511];
    }
    if (!(e & PTE_P))
        return 0;
    return RC_VM_READ | (e & PTE_W ? RC_VM_WRITE : 0) | (e & PTE_NX || !nx ? 0 : RC_VM_EXEC);
}

uint32_t arch_vm_tables(void)
{
    return tables;
}

bool arch_vm_huge(void)
{
    return huge1g;
}
