/* boot.c - die Anfragen an Limine (Protokoll-Basisrevision 6) und ihre Auswertung.
 * Alles, was der Bootloader liefert, landet hier in struct rc_boot; der übrige
 * Kernel kennt Limine nicht. */
#include "boot.h"

#include "arch.h"
#include "limine.h"

#define REQUEST __attribute__((used, section(".limine_requests"))) static volatile

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

REQUEST uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
REQUEST struct limine_bootloader_info_request info_req = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID };
REQUEST struct limine_firmware_type_request firmware_req = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST_ID };
REQUEST struct limine_hhdm_request hhdm_req = { .id = LIMINE_HHDM_REQUEST_ID };
REQUEST struct limine_framebuffer_request fb_req = { .id = LIMINE_FRAMEBUFFER_REQUEST_ID };
REQUEST struct limine_memmap_request memmap_req = { .id = LIMINE_MEMMAP_REQUEST_ID };
REQUEST struct limine_mp_request mp_req = { .id = LIMINE_MP_REQUEST_ID };
REQUEST struct limine_rsdp_request rsdp_req = { .id = LIMINE_RSDP_REQUEST_ID };
REQUEST struct limine_date_at_boot_request date_req = { .id = LIMINE_DATE_AT_BOOT_REQUEST_ID };
REQUEST struct limine_tsc_frequency_request tsc_req = { .id = LIMINE_TSC_FREQUENCY_REQUEST_ID };
REQUEST struct limine_executable_address_request exec_req = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID };
REQUEST struct limine_bootloader_performance_request perf_req = {
    .id = LIMINE_BOOTLOADER_PERFORMANCE_REQUEST_ID };

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static uint64_t hhdm;
static uint64_t alloc_top, alloc_floor;   /* Seiten für den Boot, von oben nach unten */
static uint32_t pages_used;

uint64_t boot_hhdm(void)
{
    return hhdm;
}

uint64_t boot_alloc_page(void)
{
    if (alloc_top < alloc_floor + RC_PAGE_SIZE)
        rc_panic("out of boot memory", "boot_alloc_page: no usable region left");
    alloc_top -= RC_PAGE_SIZE;
    memset((void *)(hhdm + alloc_top), 0, RC_PAGE_SIZE);
    pages_used++;
    return alloc_top;
}

uint32_t boot_pages_used(void)
{
    return pages_used;
}

static void collect_memory(struct rc_mem *m)
{
    struct limine_memmap_response *r = memmap_req.response;
    if (!r)
        rc_panic("no memory map", "the bootloader did not answer the memmap request");
    m->regions = (uint32_t)r->entry_count;
    for (uint64_t i = 0; i < r->entry_count; i++) {
        struct limine_memmap_entry *e = r->entries[i];
        switch (e->type) {
        case LIMINE_MEMMAP_USABLE:
            m->usable += e->length;
            m->usable_regions++;
            if (e->length > m->largest_run) {
                m->largest_run = e->length;
                /* Boot-Seiten kommen vom oberen Ende des größten Bereichs. */
                alloc_floor = e->base;
                alloc_top = e->base + e->length;
            }
            break;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
            m->reclaimable += e->length;
            break;
        }
    }
}

static const char *firmware_name(uint64_t type)
{
    switch (type) {
    case LIMINE_FIRMWARE_TYPE_X86BIOS:
        return "bios";
    case LIMINE_FIRMWARE_TYPE_EFI32:
        return "uefi32";
    case LIMINE_FIRMWARE_TYPE_EFI64:
        return "uefi";
    case LIMINE_FIRMWARE_TYPE_SBI:
        return "sbi";
    }
    return "unknown";
}

bool boot_collect(struct rc_boot *b)
{
    memset(b, 0, sizeof *b);
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision))
        return false;
    b->base_revision = LIMINE_LOADED_BASE_REVISION_VALID(base_revision)
                           ? LIMINE_LOADED_BASE_REVISION(base_revision)
                           : 6;

    if (!hhdm_req.response)
        rc_panic("no direct map", "the bootloader did not answer the hhdm request");
    hhdm = b->hhdm = hhdm_req.response->offset;

    if (info_req.response) {
        b->loader_name = info_req.response->name;
        b->loader_version = info_req.response->version;
    }
    b->firmware = firmware_req.response ? firmware_name(firmware_req.response->firmware_type)
                                        : "unknown";

    struct limine_framebuffer_response *fr = fb_req.response;
    if (fr && fr->framebuffer_count > 0) {
        struct limine_framebuffer *f = fr->framebuffers[0];
        b->has_fb = f->memory_model == LIMINE_FRAMEBUFFER_RGB;
        b->fb = (struct rc_fb){
            .base = f->address,
            .width = (uint32_t)f->width,
            .height = (uint32_t)f->height,
            .pitch = (uint32_t)f->pitch,
            .bpp = f->bpp,
            .red_shift = f->red_mask_shift,
            .red_size = f->red_mask_size,
            .green_shift = f->green_mask_shift,
            .green_size = f->green_mask_size,
            .blue_shift = f->blue_mask_shift,
            .blue_size = f->blue_mask_size,
        };
    }

    collect_memory(&b->mem);
    b->rsdp = rsdp_req.response ? rsdp_req.response->address : NULL;
    b->cpus = mp_req.response ? (uint32_t)mp_req.response->cpu_count : 1;
    if (date_req.response) {
        b->has_time = true;
        b->boot_unix = date_req.response->timestamp;
    }
    b->counter_hz = tsc_req.response ? tsc_req.response->frequency : 0;
    if (exec_req.response) {
        b->kernel_phys = exec_req.response->physical_base;
        b->kernel_virt = exec_req.response->virtual_base;
    }
    if (perf_req.response)
        b->loader_usec = perf_req.response->exec_usec - perf_req.response->init_usec;
    return true;
}

uint32_t boot_park_cpus(void)
{
    struct limine_mp_response *r = mp_req.response;
    uint32_t parked = 0;
    if (!r)
        return 0;
    for (uint64_t i = 0; i < r->cpu_count; i++) {
        struct limine_mp_info *cpu = r->cpus[i];
        if (cpu->lapic_id == r->bsp_lapic_id)
            continue;
        __atomic_store_n(&cpu->goto_address, arch_ap_park, __ATOMIC_SEQ_CST);
        parked++;
    }
    return parked;
}

/* ACPI-Tabellenköpfe: nur lesen, nichts auswerten (das ist Sache von devmgr). */
struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision, checksum;
    char oem[6];
} __attribute__((packed));

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem[6];
    uint8_t revision;
    uint32_t rsdt;
    uint32_t length;
    uint64_t xsdt;
} __attribute__((packed));

void boot_acpi_tables(const struct rc_boot *b, char *out, size_t cap, char *oem, size_t oem_cap)
{
    out[0] = oem[0] = '\0';
    const struct acpi_rsdp *rsdp = b->rsdp;
    if (!rsdp || memcmp(rsdp->signature, "RSD PTR ", 8) != 0)
        return;
    size_t o = 0;
    for (int i = 0; i < 6 && rsdp->oem[i] && o + 1 < oem_cap; i++)
        if (rsdp->oem[i] != ' ')
            oem[o++] = rsdp->oem[i];
    oem[o] = '\0';

    bool wide = rsdp->revision >= 2 && rsdp->xsdt;
    uint64_t phys = wide ? rsdp->xsdt : rsdp->rsdt;
    const struct acpi_header *root = (const void *)(hhdm + phys);
    if (memcmp(root->signature, wide ? "XSDT" : "RSDT", 4) != 0)
        return;
    size_t step = wide ? 8 : 4, n = (root->length - 36) / step, len = 0;
    const uint8_t *entries = (const uint8_t *)root + 36;
    for (size_t i = 0; i < n && len + 6 < cap; i++) {
        uint64_t addr = 0;
        memcpy(&addr, entries + i * step, step);
        const struct acpi_header *t = (const void *)(hhdm + addr);
        if (len)
            out[len++] = ' ';
        memcpy(out + len, t->signature, 4);
        len += 4;
    }
    out[len] = '\0';
}
