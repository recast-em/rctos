/* pmm.h - physischer Speicher in Seiten zu 4 KiB. */
#ifndef RC_PMM_H
#define RC_PMM_H

#include "kernel.h"

void pmm_init(void);
/* Eine genullte Seite; physische Adresse, 0 wenn nichts mehr frei ist. */
uint64_t pmm_alloc(void);
void pmm_free(uint64_t phys);
uint64_t pmm_free_bytes(void);
uint32_t pmm_used_pages(void);   /* vom Kernel belegt, z. B. für Seitentabellen */

#endif
