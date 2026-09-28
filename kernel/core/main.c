/* main.c - Meilenstein M0: booten, alles Wesentliche zeigen, die Budgets prüfen
 * und schlafen. Der Prozessor wacht nur auf, wenn sich die Uhr (HH:MM) ändert. */
#include "arch.h"
#include "boot.h"
#include "budget.h"
#include "buildinfo.h"
#include "cellcon.h"
#include "log.h"
#include "pmm.h"
#include "time.h"

extern char rc_image_start[], rc_text_start[], rc_text_end[], rc_rodata_start[],
    rc_rodata_end[], rc_data_start[], rc_data_end[], rc_bss_end[];

#define A_HEAD RC_ATTR(RC_WHITE, RC_BLUE)
#define A_HEAD_DIM RC_ATTR(RC_LGRAY, RC_BLUE)
#define A_LABEL RC_ATTR(RC_LGRAY, RC_BLACK)
#define A_VALUE RC_ATTR(RC_WHITE, RC_BLACK)
#define A_DIM RC_ATTR(RC_DGRAY, RC_BLACK)
#define A_BAR RC_ATTR(RC_LBLUE, RC_BLACK)
#define A_OK RC_ATTR(RC_LGREEN, RC_BLACK)
#define A_WARN RC_ATTR(RC_YELLOW, RC_BLACK)
#define A_BAD RC_ATTR(RC_LRED, RC_BLACK)

enum { COL_LABEL = 2, COL_VALUE = 13, BAR_WIDTH = 32 };

static struct rc_boot boot;
static uint64_t t_entry, idle_cycles;
static uint32_t wakes;
static int row = 2;                 /* nächste Zeile des Boot-Berichts */
static unsigned shown_minute = 99;

static void serial_line(const char *s)
{
    arch_serial_write(s, strlen(s));
    arch_serial_write("\r\n", 2);
}

/* Eine Zeile des Boot-Berichts: Bildschirm und serielle Schnittstelle. */
static void report(const char *label, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static void report(const char *label, const char *fmt, ...)
{
    char value[180], line[200];
    va_list ap;
    va_start(ap, fmt);
    rc_vfmt(value, sizeof value, fmt, ap);
    va_end(ap);
    rc_fmt(line, sizeof line, "%-10s %s", label, value);
    serial_line(line);
    con_text(COL_LABEL, row, A_LABEL, label);
    con_text(COL_VALUE, row++, A_VALUE, value);
}

static uint64_t pages(const char *start, const char *end)
{
    return RC_ALIGN_UP((uint64_t)(end - start), RC_PAGE_SIZE);
}

static uint64_t cycles_to_us(uint64_t c)
{
    uint64_t per_us = boot.counter_hz / 1000000;
    return per_us ? c / per_us : 0;
}

static void wall_now(int64_t *sec, uint64_t *nsec)
{
    uint64_t d = arch_counter() - t_entry, hz = boot.counter_hz;
    *sec = boot.boot_unix + (int64_t)(d / hz);
    *nsec = (d % hz) * 1000000000ull / hz;
}

static void draw_header(void)
{
    char right[96];
    int cols = con_cols();
    con_fill(0, 0, cols, 1, ' ', A_HEAD);
    int c = con_text(2, 0, A_HEAD, "rctos");
    con_text(c + 2, 0, A_HEAD_DIM, RC_VERSION " \xFA " RC_MILESTONE " \xFA micro environment");
    rc_fmt(right, sizeof right, "%s \xFA rev %s \xFA %s", arch_name(), RC_BUILD_REV, RC_BUILD_DATE);
    con_text(cols - 2 - (int)strlen(right), 0, A_HEAD_DIM, right);
}

/* Die Statuszeile: links der Zustand, rechts die Kachel mit Leerlauf, Uhr und Sitzung. */
static void draw_status(void)
{
    int cols = con_cols(), r = con_rows() - 1;
    char idle[32] = "idle --", clock[8] = "--:--";
    uint64_t total = arch_counter() - t_entry;
    if (wakes && total) {
        uint64_t permille = idle_cycles / (total / 100000 + 1);
        rc_fmt(idle, sizeof idle, "idle %u.%03u %%", (unsigned)(permille / 1000),
               (unsigned)(permille % 1000));
    }
    if (boot.has_time && boot.counter_hz) {
        int64_t s;
        uint64_t ns;
        struct rc_date d;
        wall_now(&s, &ns);
        rc_date_from_unix(s, &d);
        rc_fmt(clock, sizeof clock, "%02u:%02u", d.hour, d.minute);
        shown_minute = d.minute;
    }
    con_fill(0, r, cols, 1, ' ', A_LABEL);
    con_text(2, r, A_DIM, "no console yet (m2) \xFA the cpu sleeps until the clock changes");
    int c = cols - 2 - (int)(strlen(idle) + 3 + strlen(clock) + 5);
    c = con_text(c, r, A_LABEL, idle);
    c = con_text(c + 3, r, A_VALUE, clock);
    con_text(c + 2, r, A_DIM, "U -");
}

static void bar(int col, int r, uint64_t used, uint64_t total)
{
    int fill = total ? (int)((used * BAR_WIDTH + total - 1) / total) : BAR_WIDTH;
    for (int i = 0; i < BAR_WIDTH; i++)
        con_put(col + i, r, i < fill ? 0xFE : 0xFA, i < fill ? A_BAR : A_DIM);
}

static bool budget_row(const char *name, const char *used, uint64_t value, uint64_t budget,
                       const char *budget_text)
{
    char line[160];
    bool within = value <= budget;
    rc_fmt(line, sizeof line, "budget     %-11s %9s  %s", name, used, budget_text);
    serial_line(line);
    con_text(COL_VALUE, row, A_LABEL, name);
    con_text(COL_VALUE + 12 + 9 - (int)strlen(used), row, A_VALUE, used);
    bar(COL_VALUE + 24, row, value, budget);
    con_text(COL_VALUE + 26 + BAR_WIDTH, row, within ? A_LABEL : A_WARN, budget_text);
    row++;
    return within;
}

/* Die Budgets aus budget.h gegen das, was der Kernel wirklich belegt. */
static void check_budgets(uint64_t boot_us)
{
    char used[24];
    uint64_t code = pages(rc_text_start, rc_text_end) + pages(rc_rodata_start, rc_rodata_end);
    uint64_t tables = (uint64_t)arch_vm_tables() * RC_PAGE_SIZE;
    uint64_t data = pages(rc_image_start, rc_text_start) + pages(rc_data_start, rc_bss_end) +
                    (uint64_t)pmm_used_pages() * RC_PAGE_SIZE - tables;
    uint64_t image = pages(rc_image_start, rc_data_end);
    bool targets = true;

    con_text(COL_LABEL, row, A_LABEL, "budget");
    rc_fmt(used, sizeof used, "%u KiB", (unsigned)(code >> 10));
    targets &= budget_row("code+const", used, code, RC_KIB(RC_BUDGET_CODE_TARGET_KIB),
                          "of " RC_STR(RC_BUDGET_CODE_TARGET_KIB) " KiB target");
    rc_fmt(used, sizeof used, "%u KiB", (unsigned)(data >> 10));
    targets &= budget_row("data+bss", used, data, RC_KIB(RC_BUDGET_DATA_TARGET_KIB),
                          "of " RC_STR(RC_BUDGET_DATA_TARGET_KIB) " KiB target, 1 cpu");
    rc_fmt(used, sizeof used, "%u KiB", (unsigned)(tables >> 10));
    targets &= budget_row("tables", used, tables, RC_KIB(RC_BUDGET_TABLES_TARGET_KIB),
                          "of " RC_STR(RC_BUDGET_TABLES_TARGET_KIB) " KiB target, page tables");
    rc_fmt(used, sizeof used, "%u KiB", (unsigned)(image >> 10));
    targets &= image <= RC_KIB(RC_BUDGET_IMAGE_TARGET_KIB);
    bool limit = budget_row("image", used, image, RC_KIB(RC_BUDGET_IMAGE_LIMIT_KIB),
                            "of " RC_STR(RC_BUDGET_IMAGE_LIMIT_KIB) " KiB limit, target " RC_STR(
                                RC_BUDGET_IMAGE_TARGET_KIB) " KiB");
    rc_fmt(used, sizeof used, "%'u", (unsigned)RC_BUILD_LINES);
    targets &= budget_row("lines", used, RC_BUILD_LINES, RC_BUDGET_LINES_TARGET,
                          "of 10,000 target, C and assembly");
    rc_fmt(used, sizeof used, "%u.%u ms", (unsigned)(boot_us / 1000),
           (unsigned)(boot_us / 100 % 10));
    report("", "%-11s %9s  from kernel entry to idle", "boot time", used);

    row++;
    const char *verdict = !limit ? "FAILED - the image is over its hard limit"
                          : targets ? "OK - every figure within its budget"
                                    : "OK - within the limits, but over a target";
    con_text(COL_LABEL, row, A_LABEL, "gate");
    con_text(COL_VALUE, row++, !limit ? A_BAD : targets ? A_OK : A_WARN, verdict);
    serial_line(!limit ? "rctos: gate failed" : "rctos: gate ok");
}

/* Selbsttest des Speichers: Seitenverwalter und Rechte der eigenen Seitentabellen. */
static const char *memory_selftest(void)
{
    static const char text_probe[] = "rodata";
    static uint64_t data_probe;
    uint64_t a = pmm_alloc(), b = pmm_alloc();
    if (!a || !b || a == b)
        return "pmm: two allocations failed";
    if (*(uint64_t *)(boot_hhdm() + a) != 0)
        return "pmm: page not zeroed";
    pmm_free(b);
    if (pmm_alloc() != b)
        return "pmm: freed page not reused";
    pmm_free(a);
    pmm_free(b);
    if (arch_vm_query((uint64_t)memory_selftest) != (RC_VM_READ | RC_VM_EXEC))
        return "paging: code is not read+exec only";
    if (arch_vm_query((uint64_t)text_probe) != RC_VM_READ)
        return "paging: constants are not read only";
    if (arch_vm_query((uint64_t)&data_probe) != (RC_VM_READ | RC_VM_WRITE))
        return "paging: data is not read+write only";
    if (arch_vm_query(boot_hhdm() + a) != (RC_VM_READ | RC_VM_WRITE))
        return "paging: direct map is not read+write only";
    return 0;
}

static void boot_report(uint32_t parked, bool timer)
{
    char a[24], b[24], c[24], tables[128], oem[8];
    struct rc_cpu cpu;
    arch_cpu_describe(&cpu);

    report("boot", "%s %s, protocol revision %u, %s, %u ms in the bootloader",
           boot.loader_name ? boot.loader_name : "?", boot.loader_version ? boot.loader_version : "",
           (unsigned)boot.base_revision, boot.firmware, (unsigned)(boot.loader_usec / 1000));
    report("cpu", "%s, %s", cpu.vendor, cpu.brand);
    report("", "%u cpus: 1 running, %u parked in hlt; x86-64-v2 %s, nx %s, x2apic %s%s",
           boot.cpus, parked, cpu.baseline ? "yes" : "no", cpu.nx ? "yes" : "no",
           cpu.x2apic ? "yes" : "no", cpu.hypervisor ? ", under a hypervisor" : "");
    if (timer)
        report("counter", "tsc %u.%03u GHz%s; local apic timer %u.%03u MHz, one-shot",
               (unsigned)(boot.counter_hz / 1000000000),
               (unsigned)(boot.counter_hz / 1000000 % 1000),
               cpu.invariant_tsc ? " invariant" : "", (unsigned)(arch_timer_hz() / 1000000),
               (unsigned)(arch_timer_hz() / 1000 % 1000));
    else
        report("counter", "no timer - the bootloader gave no counter frequency");
    report("memory", "%s usable in %u regions, largest run %s; %s reclaimable after boot",
           rc_fmt_size(a, sizeof a, boot.mem.usable), boot.mem.usable_regions,
           rc_fmt_size(b, sizeof b, boot.mem.largest_run),
           rc_fmt_size(c, sizeof c, boot.mem.reclaimable));
    const char *fail = memory_selftest();
    if (fail)
        rc_panic("memory self-test failed", fail);
    report("paging", "own tables%s; kernel w^x: code r-x, constants r--, data rw-; %u table pages",
           arch_vm_huge() ? " with 1 GiB pages" : " with 2 MiB pages", arch_vm_tables());
    report("pages", "%s free; freed pages form a list inside themselves; self-test ok",
           rc_fmt_size(a, sizeof a, pmm_free_bytes()));
    report("display", "%u x %u x %u, pitch %u; %u x %u cells of 8 x 8%s, cp437", boot.fb.width,
           boot.fb.height, boot.fb.bpp, boot.fb.pitch, con_cols(), con_rows(),
           con_scale() > 1 ? " (doubled)" : "");
    boot_acpi_tables(&boot, tables, sizeof tables, oem, sizeof oem);
    report("acpi", "oem %s, tables %s", oem[0] ? oem : "?", tables[0] ? tables : "none");
    if (boot.has_time) {
        struct rc_date d;
        rc_date_from_unix(boot.boot_unix, &d);
        report("time", "%04d-%02u-%02u %02u:%02u:%02u utc, from the rtc via the bootloader",
               d.year, d.month, d.day, d.hour, d.minute, d.second);
    }
    report("kernel", "phys %p, virt %p; %'u lines of C and assembly", (void *)boot.kernel_phys,
           (void *)boot.kernel_virt, (unsigned)RC_BUILD_LINES);
    row++;
}

static void arm_next_minute(void)
{
    int64_t s;
    uint64_t ns;
    wall_now(&s, &ns);
    arch_timer_arm_ns((uint64_t)(60 - s % 60) * 1000000000ull - ns + 1000000);
}

/* Schlafen, bis der Zeitgeber feuert; die Zeit im hlt zählt als Leerlauf. */
static _Noreturn void idle_loop(void)
{
    for (;;) {
        arm_next_minute();
        arch_irq_disable();
        while (!arch_timer_fired) {
            uint64_t t0 = arch_counter();
            arch_wait();
            idle_cycles += arch_counter() - t0;
            arch_irq_disable();
        }
        arch_timer_fired = 0;
        arch_irq_enable();
        wakes++;
        int64_t s;
        uint64_t ns;
        struct rc_date d;
        wall_now(&s, &ns);
        rc_date_from_unix(s, &d);
        if (d.minute != shown_minute) {
            draw_status();
            uint64_t total = arch_counter() - t_entry;
            uint64_t permille = idle_cycles / (total / 100000 + 1);
            klog("%02u:%02u  the clock changed; wake %u, idle %u.%03u %% since boot", d.hour,
                 d.minute, wakes, (unsigned)(permille / 1000), (unsigned)(permille % 1000));
        }
    }
}

_Noreturn void kmain(void)
{
    t_entry = arch_counter();
    arch_early_init();
    serial_line("rctos " RC_VERSION " " RC_MILESTONE " (rev " RC_BUILD_REV ")");
    if (!boot_collect(&boot))
        rc_panic("bootloader too old", "rctos needs the limine protocol, base revision 6");
    arch_init();
    pmm_init();
    if (!boot.kernel_phys)
        rc_panic("no kernel address", "the bootloader did not report where the kernel lies");
    arch_vm_init(boot.kernel_phys, boot.kernel_virt);

    bool screen = boot.has_fb && con_init(&boot.fb, false);
    if (screen)
        draw_header();
    else
        serial_line("no usable framebuffer - serial output only");

    uint32_t parked = boot_park_cpus();
    bool timer = boot.counter_hz && boot.has_time && arch_timer_init(boot.counter_hz);
    boot_report(parked, timer);
    uint64_t boot_us = cycles_to_us(arch_counter() - t_entry);
    check_budgets(boot_us);

    if (screen) {
        int rows = con_rows(), top = row + 1;
        con_fill(0, top, con_cols(), 1, 0xC4, A_DIM);
        con_text(COL_LABEL, top, A_DIM, " log ");
        klog_area(top + 1, rows - 2, A_LABEL);
        con_fill(0, rows - 2, con_cols(), 1, 0xC4, A_DIM);
        draw_status();
    }
    if (!timer) {
        klog("no timer - nothing left to do, the cpu halts for good");
        serial_line("rctos: idle");
        arch_halt_forever();
    }
    int64_t s;
    uint64_t ns;
    struct rc_date now, next;
    wall_now(&s, &ns);
    rc_date_from_unix(s, &now);
    rc_date_from_unix(s - s % 60 + 60, &next);
    klog("%02u:%02u  ready in %u.%u ms; the cpu sleeps until %02u:%02u", now.hour, now.minute,
         (unsigned)(boot_us / 1000), (unsigned)(boot_us / 100 % 10), next.hour, next.minute);
    serial_line("rctos: idle");
    idle_loop();
}
