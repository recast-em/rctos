# 02 – Syscalls und Kits

## Die drei Kits

Alle APIs sind **flache C-Funktionen**: keine Objekte, keine Callbacks, kein versteckter
Zustand. Jedes Kit ist ein Header und eine statische Bibliothek.

| Kit | Präfix | Bibliothek | Umsetzung | Wer darf es nutzen |
|---|---|---|---|---|
| **System-Kit** | `sys_` | `libsys.a` | 1:1 auf Kernel-Syscalls | jeder Prozess |
| **Treiber-Kit** | `drv_` | `libdrv.a` | 1:1 auf Kernel-Syscalls | jeder Prozess, wirksam aber nur mit passender `Resource`, `Bti` oder `Msi` |
| **User-Kit** | `usr_` | `libusr.a` | flache Stubs, die Nachrichten an Dienste schicken | jeder Prozess mit Kanal zum jeweiligen Dienst |

Das User-Kit sieht für eine Anwendung genauso aus wie die beiden anderen Kits, also wie
eine Funktion, die man aufruft. Dateien, Netz, Anzeige und Eingabe gehören aber nicht in
den Kernel, sonst ließe sich das Größenziel nicht halten. Deshalb übersetzt ein
`usr_`-Aufruf in eine Kanalnachricht an den zuständigen Dienst. Der Nummernkreis 0x180–0x1FF
bleibt für den Fall reserviert, dass ein User-Kit-Aufruf nachweislich einen eigenen
Syscall braucht.

Eine Unterscheidung wie „privilegiertes Treiber-Kit“ gibt es nicht. Die Aufrufe sind für
alle da, aber ohne das passende Ressourcen-Handle schlagen sie mit
`RC_ERR_ACCESS_DENIED` fehl. Die Rechte liegen also im Handle, nicht im Prozesstyp.

## Binärschnittstelle

| | x86-64 | AArch64 |
|---|---|---|
| Befehl | `syscall` | `svc #0` |
| Syscall-Nummer | `rax` | `x8` |
| Argumente 1–6 | `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` | `x0`–`x5` |
| Rückgabe | `rax` (`rc_status_t`) | `x0` |
| Vom Kernel verändert | `rcx`, `r11` | keine außer `x0` |

- Es gibt höchstens 6 Argumente. Wer mehr braucht, übergibt einen Zeiger auf ein Struct.
- Jeder Aufruf gibt `rc_status_t` zurück. Ergebnisse landen in Ausgabezeigern.
- Die Structs haben ein festes Layout mit natürlicher Ausrichtung und expliziten
  Füllfeldern, die Null sein müssen. Mit Blick auf eine spätere Erweiterung beginnt jedes
  Struct mit `uint32_t size`.

### Basistypen

```c
typedef uint32_t rc_handle_t;    /* 0 = RC_HANDLE_INVALID                   */
typedef int32_t  rc_status_t;    /* 0 = RC_OK, < 0 = Fehler                 */
typedef uint32_t rc_rights_t;
typedef uint32_t rc_signals_t;
typedef uint64_t rc_time_t;      /* ns, monoton; RC_TIME_INFINITE = ~0ull   */
typedef uint64_t rc_paddr_t;
typedef uint64_t rc_off_t;
```

### Fehlercodes

| Code | Wert | Bedeutung |
|---|---|---|
| `RC_OK` | 0 | Erfolg |
| `RC_ERR_INVALID_ARGS` | −1 | ungültige Argumente oder Zeiger |
| `RC_ERR_BAD_HANDLE` | −2 | Handle existiert nicht |
| `RC_ERR_WRONG_TYPE` | −3 | falscher Objekttyp |
| `RC_ERR_ACCESS_DENIED` | −4 | Recht fehlt |
| `RC_ERR_NO_MEMORY` | −5 | kein Speicher frei |
| `RC_ERR_QUOTA` | −6 | Kontingent erschöpft |
| `RC_ERR_TIMED_OUT` | −7 | Termin verstrichen |
| `RC_ERR_SHOULD_WAIT` | −8 | würde blockieren (nicht blockierender Aufruf) |
| `RC_ERR_PEER_CLOSED` | −9 | Gegenseite geschlossen |
| `RC_ERR_BUFFER_TOO_SMALL` | −10 | Puffer zu klein; nötige Größe steht im Ausgabefeld |
| `RC_ERR_NOT_SUPPORTED` | −11 | unbekannter Syscall oder Funktion nicht vorhanden |
| `RC_ERR_BAD_STATE` | −12 | Objekt im falschen Zustand |
| `RC_ERR_OUT_OF_RANGE` | −13 | Offset oder Adresse außerhalb des Bereichs |
| `RC_ERR_REVOKED` | −14 | Objekt wurde widerrufen |
| `RC_ERR_CANCELED` | −15 | Wartevorgang abgebrochen |

## System-Kit (0x000–0x0FF)

### Handles und Objekte

| Nr. | Aufruf |
|---|---|
| 0x000 | `sys_handle_close(rc_handle_t h)` |
| 0x001 | `sys_handle_duplicate(rc_handle_t h, rc_rights_t rights, rc_handle_t *out)` |
| 0x002 | `sys_handle_replace(rc_handle_t h, rc_rights_t rights, rc_handle_t *out)` |
| 0x003 | `sys_object_info(rc_handle_t h, uint32_t topic, void *buf, size_t len)` |
| 0x004 | `sys_object_wait_one(rc_handle_t h, rc_signals_t s, rc_time_t deadline, rc_signals_t *observed)` |
| 0x005 | `sys_object_wait_many(rc_wait_item_t *items, uint32_t n, rc_time_t deadline)` |
| 0x006 | `sys_object_signal(rc_handle_t h, rc_signals_t clear, rc_signals_t set)` |
| 0x007 | `sys_object_signal_peer(rc_handle_t h, rc_signals_t clear, rc_signals_t set)` |

`sys_handle_duplicate` und `sys_handle_replace` können Rechte nur **wegnehmen**.
`RC_RIGHT_SAME` übernimmt die bisherigen Rechte.

### Prozesse und Threads

| Nr. | Aufruf |
|---|---|
| 0x010 | `sys_process_create(const rc_process_args_t *args, rc_handle_t *proc, rc_handle_t *aspace)` |
| 0x011 | `sys_process_start(rc_handle_t proc, rc_handle_t thread, uintptr_t entry, uintptr_t stack, rc_handle_t bootstrap)` |
| 0x012 | `sys_process_exit(int64_t code)` – kehrt nicht zurück |
| 0x013 | `sys_task_kill(rc_handle_t process_or_thread)` |
| 0x014 | `sys_thread_create(rc_handle_t proc, const char *name, size_t len, rc_handle_t *thread)` |
| 0x015 | `sys_thread_start(rc_handle_t thread, uintptr_t entry, uintptr_t stack, uintptr_t arg1, uintptr_t arg2)` |
| 0x016 | `sys_thread_exit(void)` – kehrt nicht zurück |
| 0x017 | `sys_thread_set_priority(rc_handle_t thread, uint32_t prio)` |
| 0x018 | `sys_thread_yield(void)` |
| 0x019 | `sys_thread_sleep(rc_time_t deadline)` |
| 0x01A | `sys_thread_set_tls(uintptr_t base)` – für x86-64 (FS-Basis); auf AArch64 überflüssig, aber erlaubt |

`rc_process_args_t` enthält den Namen und das Kontingent: Seiten, Handles und höchste
Priorität. Das Kontingent wird vom Elternprozess abgezogen.

### Speicher

| Nr. | Aufruf |
|---|---|
| 0x020 | `sys_vmo_create(uint64_t size, uint32_t flags, rc_handle_t *vmo)` |
| 0x021 | `sys_vmo_read(rc_handle_t vmo, void *buf, rc_off_t off, size_t len)` |
| 0x022 | `sys_vmo_write(rc_handle_t vmo, const void *buf, rc_off_t off, size_t len)` |
| 0x023 | `sys_vmo_set_size(rc_handle_t vmo, uint64_t size)` |
| 0x024 | `sys_vmo_op(rc_handle_t vmo, uint32_t op, rc_off_t off, uint64_t len)` – `COMMIT`, `DECOMMIT`, `ZERO` |
| 0x025 | `sys_vmo_create_slice(rc_handle_t vmo, rc_off_t off, uint64_t len, uint32_t flags, rc_handle_t *slice)` |
| 0x026 | `sys_vmo_revoke(rc_handle_t slice)` |
| 0x027 | `sys_vmo_make_executable(rc_handle_t vmo, rc_handle_t exec_resource, rc_handle_t *out)` |
| 0x028 | `sys_vm_reserve(rc_handle_t region, uintptr_t addr, uint64_t size, uint32_t flags, rc_handle_t *sub, uintptr_t *out_addr)` |
| 0x029 | `sys_vm_map(const rc_map_args_t *args, uintptr_t *out_addr)` |
| 0x02A | `sys_vm_unmap(rc_handle_t region, uintptr_t addr, uint64_t len)` |
| 0x02B | `sys_vm_protect(rc_handle_t region, uintptr_t addr, uint64_t len, uint32_t perms)` |

Ein **Ausschnitt** (`slice`) ist ein widerrufbares Fenster auf ein VMO. `sys_vmo_revoke`
entfernt alle Einblendungen des Ausschnitts in allen Prozessen und macht jedes Handle auf
ihn ungültig. Auf diesem Mechanismus beruht der widerrufbare Direktzugriff auf den
Framebuffer ([03](03-rechte.md#direktzugriff)).

### IPC

| Nr. | Aufruf |
|---|---|
| 0x030 | `sys_channel_create(uint32_t flags, rc_handle_t *a, rc_handle_t *b)` |
| 0x031 | `sys_channel_write(rc_handle_t ch, const rc_msg_t *msg)` |
| 0x032 | `sys_channel_read(rc_handle_t ch, rc_msg_t *msg)` |
| 0x033 | `sys_channel_call(rc_handle_t ch, rc_call_args_t *args, rc_time_t deadline)` |
| 0x034 | `sys_event_create(uint32_t flags, rc_handle_t *ev)` |
| 0x035 | `sys_eventpair_create(uint32_t flags, rc_handle_t *a, rc_handle_t *b)` |
| 0x036 | `sys_port_create(uint32_t flags, rc_handle_t *port)` |
| 0x037 | `sys_port_bind(rc_handle_t port, rc_handle_t obj, uint64_t key, rc_signals_t s, uint32_t flags)` |
| 0x038 | `sys_port_wait(rc_handle_t port, rc_time_t deadline, rc_packet_t *pkts, uint32_t max, uint32_t *n)` |
| 0x039 | `sys_port_queue(rc_handle_t port, const rc_packet_t *pkt)` |
| 0x03A | `sys_futex_wait(const uint32_t *addr, uint32_t expected, rc_time_t deadline)` |
| 0x03B | `sys_futex_wake(const uint32_t *addr, uint32_t count)` |

```c
typedef struct {
    void        *bytes;      /* rein: Puffer; bei read: Zielpuffer     */
    rc_handle_t *handles;
    uint32_t     num_bytes;  /* write: Länge; read: rein Kapazität, raus Länge */
    uint32_t     num_handles;
} rc_msg_t;
```

### Zeit und System

| Nr. | Aufruf |
|---|---|
| 0x040 | `sys_clock_get(uint32_t clock_id, rc_time_t *out)` – `libsys` nutzt, wenn möglich, die Zeitseite |
| 0x041 | `sys_timer_create(uint32_t flags, rc_handle_t *timer)` |
| 0x042 | `sys_timer_set(rc_handle_t timer, rc_time_t deadline, rc_time_t slack)` |
| 0x043 | `sys_timer_cancel(rc_handle_t timer)` |
| 0x050 | `sys_random_get(void *buf, size_t len)` |
| 0x051 | `sys_system_info(uint32_t topic, void *buf, size_t len)` – ABI-Stand, CPUs, Seitengröße, Speicherstatistik |
| 0x052 | `sys_log_write(rc_handle_t log, const void *buf, size_t len)` |
| 0x053 | `sys_log_read(rc_handle_t log, void *buf, size_t len, size_t *actual)` |

Das System-Kit hat **51 Aufrufe**.

## Treiber-Kit (0x100–0x17F)

| Nr. | Aufruf | Benötigt |
|---|---|---|
| 0x100 | `drv_resource_create(rc_handle_t parent, uint32_t kind, uint64_t base, uint64_t size, rc_handle_t *out)` | Elternressource, die den Bereich umfasst |
| 0x101 | `drv_vmo_create_physical(rc_handle_t mmio_res, rc_paddr_t base, uint64_t size, rc_handle_t *vmo)` | MMIO-Ressource |
| 0x102 | `drv_vmo_set_cache_policy(rc_handle_t vmo, uint32_t policy)` | VMO mit `MANAGE`, noch nicht eingeblendet |
| 0x103 | `drv_vmo_create_contiguous(rc_handle_t bti, uint64_t size, uint32_t align_log2, rc_handle_t *vmo)` | BTI |
| 0x104 | `drv_ioport_enable(rc_handle_t io_res, uint16_t port, uint16_t count)` | IO-Port-Ressource (nur x86-64) |
| 0x105 | `drv_interrupt_create(rc_handle_t src, uint32_t index, uint32_t flags, rc_handle_t *irq)` | IRQ-Ressource oder `Msi` |
| 0x106 | `drv_interrupt_wait(rc_handle_t irq, rc_time_t *timestamp)` | – |
| 0x107 | `drv_interrupt_ack(rc_handle_t irq)` | – |
| 0x108 | `drv_interrupt_bind(rc_handle_t irq, rc_handle_t port, uint64_t key)` | – |
| 0x109 | `drv_msi_allocate(rc_handle_t irq_res, uint32_t count, rc_handle_t *msi)` | IRQ-Ressource |
| 0x10A | `drv_bti_create(rc_handle_t iommu_res, uint64_t bus_id, rc_handle_t *bti)` | IOMMU-Ressource |
| 0x10B | `drv_bti_pin(const rc_pin_args_t *args, rc_handle_t *pmt)` | BTI, VMO |
| 0x10C | `drv_pmt_unpin(rc_handle_t pmt)` | – |
| 0x10D | `drv_cache_op(uintptr_t addr, size_t len, uint32_t op)` | – (auf x86-64 ohne Wirkung) |
| 0x10E | `drv_smc_call(rc_handle_t smc_res, const rc_smc_args_t *in, rc_smc_args_t *out)` | SMC-Ressource (nur AArch64) |
| 0x10F | `drv_system_power(rc_handle_t power_res, uint32_t action)` | Energie-Ressource (Neustart, Ausschalten) |

Das Treiber-Kit hat **16 Aufrufe**. Die MSI-Adresse und die Datenwerte, die der
PCI-Bustreiber ins Gerät schreibt, liefert `sys_object_info(msi, RC_INFO_MSI, …)`.

## User-Kit (flache Dienst-Stubs)

Jede Funktion baut eine Nachricht, ruft `sys_channel_call` auf dem passenden Dienstkanal
auf und gibt das Ergebnis zurück. Die Dienstkanäle holt `libusr` beim Start einmalig über
den Namensraum.

| Gruppe | Beispiele | Dienst |
|---|---|---|
| Dienste | `usr_service_connect(name, len, &ch)` | Namensdienst in `init` |
| Prozesse | `usr_process_spawn(path, argv, handles, &proc)` | Starter in `init` |
| Dateien | `usr_file_open`, `usr_file_read`, `usr_file_write`, `usr_file_stat`, `usr_dir_read`, `usr_file_get_vmo` | Dateidienst |
| Netz | `usr_socket_create`, `usr_socket_connect`, `usr_socket_send`, `usr_socket_recv`, … | Netzdienst |
| Anzeige | `usr_display_list`, `usr_display_lease`, `usr_display_map_framebuffer`, `usr_display_present` | Display-Treiber |
| Eingabe | `usr_input_open`, `usr_input_read`, `usr_input_grab` | Eingabedienst |
| GPU | `usr_gpu_open`, `usr_gpu_query`, … (für Treibermodule, [05](05-grafik.md)) | GPU-Treiber |
| Audio | später | Audiodienst |

Die POSIX-Schicht der libc ([06](06-chromium.md)) baut auf dem User-Kit auf. Das User-Kit
selbst hängt von keiner libc ab.

## Stabilitätsregeln („keine DLL-Hölle“)

1. **Nummern werden nie wiederverwendet.** Ein entfernter Aufruf liefert für immer
   `RC_ERR_NOT_SUPPORTED`.
2. **Die Bedeutung eines Aufrufs ändert sich nie.** Neues Verhalten bekommt eine neue
   Nummer, etwa `sys_vm_map2`.
3. **Structs wachsen nur hinten.** Das Feld `size` sagt dem Kernel, welche Version vorliegt.
4. **Es gibt kein dynamisches Linken der Kits.** Programme binden die Kits statisch ein.
   Ein Programm von heute läuft auf jedem künftigen Kernel. Auf einem älteren Kernel
   bekommt es für neue Aufrufe `RC_ERR_NOT_SUPPORTED` und kann darauf reagieren.
   `sys_system_info(RC_INFO_ABI)` liefert den ABI-Stand.
5. **Protokolle folgen denselben Regeln:** Nachrichtennummern (Ordinals) werden nie
   wiederverwendet, Nachrichten wachsen nur hinten, und beim Verbindungsaufbau wird die
   Version ausgehandelt ([04](04-treibermodell.md#protokolle)).

## Beispiel

```c
#include <rc/sys.h>

/* Einen Kanal anlegen, eine Nachricht mit einem VMO senden. */
rc_handle_t a, b, vmo;
sys_channel_create(0, &a, &b);
sys_vmo_create(64 * 1024, 0, &vmo);

rc_msg_t msg = { .bytes = "hallo", .num_bytes = 5,
                 .handles = &vmo,   .num_handles = 1 };
rc_status_t st = sys_channel_write(a, &msg);   /* vmo gehört jetzt dem Empfänger */
```
