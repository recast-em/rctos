/* pmm.c - physischer Speicher. Freigegebene Seiten bilden eine Liste in sich selbst
 * (der Verweis auf die nächste steht in der Seite); noch nie benutzte Seiten werden von
 * den freien Bereichen der Speicherkarte abgeschnitten. Keine Metadaten je Seite, und
 * beim Start wird der RAM nicht angefasst. */
#include "pmm.h"

#include "boot.h"

#define MAX_RUNS 64

struct run {
    uint64_t base, end;   /* [base, end), seitenweise ausgerichtet */
};

static struct run runs[MAX_RUNS];
static uint32_t nruns, used, listed;
static uint64_t head;     /* erste Seite der Liste, 0 = leer */
static uint64_t untouched;

void pmm_init(void)
{
    uint64_t base, len;
    enum rc_memtype type;
    for (uint32_t i = 0; boot_memmap(i, &base, &len, &type); i++) {
        if (type != RC_MEM_USABLE)
            continue;
        uint64_t b = RC_ALIGN_UP(base, RC_PAGE_SIZE), e = (base + len) & ~(uint64_t)4095;
        if (b == 0)
            b = RC_PAGE_SIZE;   /* Seite 0 bleibt frei von Bedeutung: 0 heißt "keine Seite" */
        if (e <= b)
            continue;
        if (nruns == MAX_RUNS)
            rc_panic("memory map", "more than 64 usable regions");
        runs[nruns++] = (struct run){ b, e };
        untouched += e - b;
    }
}

uint64_t pmm_alloc(void)
{
    uint64_t page = 0;
    if (head) {
        page = head;
        head = *(uint64_t *)(boot_hhdm() + page);
        listed--;
    } else {
        /* von oben abschneiden: niedrige Adressen bleiben für Geräte mit Grenzen frei */
        for (uint32_t i = nruns; i-- > 0;) {
            if (runs[i].end > runs[i].base) {
                runs[i].end -= RC_PAGE_SIZE;
                page = runs[i].end;
                untouched -= RC_PAGE_SIZE;
                break;
            }
        }
        if (!page)
            return 0;
    }
    memset((void *)(boot_hhdm() + page), 0, RC_PAGE_SIZE);
    used++;
    return page;
}

void pmm_free(uint64_t phys)
{
    *(uint64_t *)(boot_hhdm() + phys) = head;
    head = phys;
    listed++;
    used--;
}

uint64_t pmm_free_bytes(void)
{
    return untouched + (uint64_t)listed * RC_PAGE_SIZE;
}

uint32_t pmm_used_pages(void)
{
    return used;
}
