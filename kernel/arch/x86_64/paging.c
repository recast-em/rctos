/* paging.c - eine MMIO-Region in die laufenden Seitentabellen eintragen, an der
 * Stelle der Direktabbildung (hhdm + phys), ungecacht. Fehlende Tabellen kommen
 * aus boot_alloc_page. Eigene Seitentabellen baut erst M1. */
#include "arch.h"
#include "boot.h"
#include "x86.h"

#define PTE_P (1ull << 0)
#define PTE_W (1ull << 1)
#define PTE_PWT (1ull << 3)
#define PTE_PCD (1ull << 4)
#define PTE_PS (1ull << 7)
#define PTE_NX (1ull << 63)
#define PTE_ADDR 0x000FFFFFFFFFF000ull
#define MSR_EFER 0xC0000080

static uint64_t *table(uint64_t phys)
{
    return (uint64_t *)(boot_hhdm() + (phys & PTE_ADDR));
}

static uint64_t *descend(uint64_t *entry)
{
    if (!(*entry & PTE_P))
        *entry = boot_alloc_page() | PTE_P | PTE_W;
    else if (*entry & PTE_PS)
        rc_panic("mmio mapping", "a large page already covers the mmio region");
    return table(*entry);
}

void *arch_map_mmio(uint64_t phys, uint64_t size)
{
    if (read_cr4() & (1u << 12))
        rc_panic("mmio mapping", "5-level paging is not supported yet");
    uint64_t nx = rdmsr(MSR_EFER) & (1u << 11) ? PTE_NX : 0;
    uint64_t start = phys & PTE_ADDR, end = RC_ALIGN_UP(phys + size, RC_PAGE_SIZE);
    for (uint64_t p = start; p < end; p += RC_PAGE_SIZE) {
        uint64_t va = boot_hhdm() + p;
        uint64_t *pml4 = table(read_cr3());
        uint64_t *pdpt = descend(&pml4[va >> 39 & 511]);
        uint64_t *pd = descend(&pdpt[va >> 30 & 511]);
        uint64_t *pt = descend(&pd[va >> 21 & 511]);
        pt[va >> 12 & 511] = p | PTE_P | PTE_W | PTE_PWT | PTE_PCD | nx;
        invlpg((void *)va);
    }
    return (void *)(boot_hhdm() + phys);
}
