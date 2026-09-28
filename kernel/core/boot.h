/* boot.h - was der Bootloader übergibt, architekturunabhängig aufbereitet. */
#ifndef RC_BOOT_H
#define RC_BOOT_H

#include "cellcon.h"
#include "kernel.h"

struct rc_mem {
    uint64_t usable;        /* frei nutzbarer RAM */
    uint64_t reclaimable;   /* nach dem Boot zurückzuholen (Bootloader, ACPI) */
    uint64_t largest_run;   /* größter zusammenhängender freier Bereich */
    uint32_t regions, usable_regions;
};

struct rc_boot {
    const char *loader_name, *loader_version;
    uint64_t base_revision;
    const char *firmware;           /* "uefi", "bios", ... */
    uint64_t hhdm;                  /* Beginn der Direktabbildung */
    bool has_fb;
    struct rc_fb fb;
    struct rc_mem mem;
    void *rsdp;
    uint32_t cpus;
    bool has_time;
    int64_t boot_unix;              /* Uhrzeit beim Boot (UTC, aus der RTC) */
    uint64_t counter_hz;            /* Frequenz von arch_counter(), 0 = unbekannt */
    uint64_t loader_usec;           /* Zeit im Bootloader */
    uint64_t kernel_phys, kernel_virt;
};

/* Liest alle Antworten des Bootloaders; false, wenn das Protokoll nicht passt. */
bool boot_collect(struct rc_boot *b);
uint64_t boot_hhdm(void);
/* Die Speicherkarte, Eintrag für Eintrag. */
enum rc_memtype {
    RC_MEM_USABLE,       /* frei */
    RC_MEM_LOADER,       /* Bootloader-Daten, später zurückzuholen */
    RC_MEM_KERNEL,       /* Kernel und Module */
    RC_MEM_FIRMWARE,     /* ACPI und Firmware-Laufzeit: abbilden, nicht anfassen */
    RC_MEM_FRAMEBUFFER,
    RC_MEM_UNMAPPED,     /* reserviert oder defekt */
};
bool boot_memmap(uint32_t i, uint64_t *base, uint64_t *len, enum rc_memtype *type);
/* Weitere CPUs schlafen legen; gibt ihre Zahl zurück. */
uint32_t boot_park_cpus(void);
/* ACPI: Signaturen der Tabellen als "FACP APIC HPET ..." */
void boot_acpi_tables(const struct rc_boot *b, char *out, size_t cap, char *oem, size_t oem_cap);

#endif
