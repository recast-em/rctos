# 04 – Treibermodell

## Grundsatz

**Ein Treiber ist ein Prozess.** Er bekommt die Ressourcen seines Geräts als Handles und
bietet eine oder mehrere **Geräteklassen** über Kanäle an. Clients sprechen nur das
Protokoll der Klasse. Welcher Treiber dahinter steckt, sehen sie nicht und müssen es nicht
wissen. Deshalb lässt sich ein Treiber austauschen, ohne einen Client anzufassen.

```
            ┌──────────────── Namensdienst ─────────────────┐
            │  /dev/display/0   /dev/gpu/0   /dev/input/0    │
            └──────▲──────────────▲──────────────▲──────────┘
                   │ veröffentlicht               │
 ┌─────────────────┴──────────────┴──┐   ┌───────┴───────┐
 │ intel-igpu (Prozess)              │   │ usb-hid       │
 │  Protokolle: display, gpu         │   │  Prot.: input │
 └──────────────▲────────────────────┘   └───────▲───────┘
                │ Ressourcen + pci-Kanal          │ usb-Kanal
        ┌───────┴────────┐                ┌──────┴──────┐
        │ pci (Bus)      │───────────────►│ xhci (Bus)  │
        └───────▲────────┘                └─────────────┘
                │ ECAM, MSI-Pool
        ┌───────┴────────┐
        │ devmgr         │  liest ACPI-Tabellen bzw. DTB, startet Treiber
        └────────────────┘
```

## Gerätemanager (`devmgr`)

`devmgr` erledigt vier Dinge:

1. **Plattform auswerten.** Auf x86-64 liest er ACPI-Tabellen (MADT, MCFG, HPET, FADT,
   DMAR), aber noch keine AML ([08](08-plattformen.md)). Auf AArch64 liest er den Device Tree.
   Daraus entstehen Wurzelgeräte, etwa „PCI-Host an ECAM x“ oder „UART an 0x3F8“.
2. **Treiber auswählen** über die Match-Regeln der installierten Treiber-Manifeste.
3. **Treiber starten** mit genau den Ressourcen des Geräts.
4. **Überwachen.** Stirbt ein Treiber, startet `devmgr` ihn neu oder wählt den
   nächsten passenden Treiber aus der Rückfallkette.

Bustreiber wie `pci`, `xhci` oder `i2c-hid` sind normale Treiber. Sie melden ihre
Kindgeräte über das Protokoll `devmgr.publish_device` zurück, und `devmgr` sucht dafür
wieder passende Treiber.

**Auskunft:** `devmgr` führt zwei Tabellen, die unter `/now` lesbar sind: `/now/devs` (jedes
Gerät mit Treiber, Klasse, Zustand und Bus) und `/now/res` (das Ressourcenbuch: welcher
MMIO-Bereich, welcher IRQ, welcher IO-Port gehört wem). Welcher Treiber gerade was belegt,
ist damit jederzeit mit `cat` zu sehen ([09](09-mikro-welt.md#now--der-zustand-als-tabellen)).

## Treiber-Manifest

```ini
; /sys/drivers/intel-igpu/manifest
name       = intel-igpu
binary     = intel-igpu
class      = display gpu
priority   = 20                ; Vorrang vor efifb (Priorität 1)
memory     = 2M                ; Kontingent des Treiberprozesses
module     = vulkan_intel.so   ; optional: Treibermodul, siehe unten
module_abi = vulkan-icd-5
match_pci  = 8086:* 0300       ; Intel, VGA-Controller; die Gerätetabelle im Treiber entscheidet
pci_bars   = 0 2
msi        = 1
dma        = yes
```

Manifeste haben das Format von `main.cfg`: eine Zeile pro Schlüssel, keine Abschnitte,
Kommentare mit `;` ([09](09-mikro-welt.md#maincfg)). Mögliche Match-Schlüssel sind
`match_pci` (Hersteller:Gerät, Klasse), `match_acpi` (HID/CID), `match_dt` (`compatible`),
`match_usb` (Hersteller:Produkt, Klasse) und `match_virtio` (Gerätetyp). Passen mehrere
Treiber, entscheidet die höchste `priority`. Zusätzlich kann ein Treiber nach dem Start mit
`NOT_SUPPORTED` antworten, etwa weil die Generation nicht passt. Dann nimmt `devmgr` den
nächsten.

**Jeder Treiber hat ein Budget.** `memory` ist sein Kontingent, und `/now/tasks` zeigt, wie
viel er davon wirklich belegt. Ein Treiber, der mehr will, bekommt `RC_ERR_QUOTA` und kein
stilles Wachstum.

## Start eines Treibers

Die Bootstrap-Nachricht an einen Treiber enthält:

| Handle | Inhalt |
|---|---|
| `parent` | Kanal zum Bustreiber (z. B. PCI-Protokoll: Konfigurationsraum lesen und schreiben, Bus-Master ein) |
| `mmio[n]` | MMIO-Ressourcen der freigegebenen BARs |
| `irq` | MSI-Block oder IRQ-Ressource |
| `bti` | DMA-Identität, falls `dma = yes` |
| `publish` | Kanal zum Namensdienst, über den der Treiber seine Klassen veröffentlicht |
| `log` | Log-Handle |

Mehr bekommt ein Treiber nicht. Er hat weder einen Dateizugriff (außer auf sein eigenes
Paketverzeichnis, etwa für Firmware) noch Netz oder Zugriff auf fremde Geräte.

## Protokolle

### Nachrichtenformat

```c
typedef struct {
    uint32_t txid;       /* von sys_channel_call genutzt; 0 = Ereignis     */
    uint32_t ordinal;    /* Nachrichtennummer innerhalb des Protokolls     */
    uint32_t flags;
    uint32_t reserved;   /* 0                                               */
} rc_proto_header_t;     /* 16 Byte, danach die Nutzdaten als festes Struct */
```

- **Die erste Nachricht** auf jedem Kanal ist `HELLO` mit Protokollname und
  Versionsbereich. Die Antwort nennt die höchste gemeinsame Version.
- Die Regeln sind dieselben wie für Syscalls ([02](02-syscalls-und-kits.md#stabilitätsregeln-keine-dll-hölle)):
  Ordinals werden nie wiederverwendet, Nutzdaten-Structs wachsen nur hinten.
- Protokolle stehen als C-Header in `protocols/<name>.h`, mit einem kurzen Textteil zu
  Reihenfolge und Fehlerfällen. Einen Generator gibt es erst, wenn sich die Handarbeit
  nicht mehr lohnt ([00](00-vision.md#offene-entscheidungen)).

### Geräteklassen (Phase 1)

| Klasse | Kernnachrichten | Clients |
|---|---|---|
| `block` | `info`, `read`, `write`, `flush` (Daten per VMO, Ringpuffer optional) | Dateidienst |
| `net` | `info`, `set_rx_vmo`, `tx`, Ereignis `rx`, `link_state` | Netzdienst |
| `input` | `info`, Ereignisse `key`, `pointer`, `touch` (evdev-ähnlich) | Eingabedienst |
| `serial` | `write`, Ereignis `rx`, `config` | Konsole, Log |
| `display` | siehe [05](05-grafik.md#anzeige-protokoll) | Chromium, Kiosk-Programme |
| `gpu` | siehe [05](05-grafik.md#gpu-protokoll) | Treibermodule (Vulkan-ICD) |
| `pci`, `usb`, `i2c` | Bus-Protokolle zwischen Bus- und Gerätetreiber | Treiber |

Gemeinsamer Code für alle Treiber einer Klasse, etwa Lease-Verwaltung oder
Ringpuffer-Logik, liegt als **statische Bibliothek** vor und wird in jeden Treiber
eingebunden. Eine Zwischenschicht als eigener Prozess gibt es dafür nicht.

## Austausch und Rückfall

- **Austausch:** Man legt ein neues Treiberpaket ab und startet den Treiber neu, entweder
  mit `devmgr restart <gerät>` oder durch einen Neustart des Systems. Clients sehen
  `PEER_CLOSED`, bauen die Verbindung über den Namensdienst neu auf und bekommen den neuen
  Treiber. Das Protokoll ist dasselbe.
- **Rückfallketten:** Für jede Klasse gibt es einen generischen Treiber, der immer
  funktioniert:

  | Klasse | bevorzugt | Rückfall |
  |---|---|---|
  | `display` | `intel-igpu`, `virtio-gpu`, … | `efifb` (Firmware-Framebuffer, keine Modi, keine Beschleunigung) |
  | `gpu` | `intel-igpu`, `virtio-gpu` | keiner. Chromium nutzt dann SwiftShader (Vulkan auf der CPU) |
  | `input` | `usb-hid`, `i2c-hid` | `ps2` |
  | `block` | `nvme`, `ahci` | `virtio-blk` (QEMU) |

- **Absturz:** `devmgr` startet einen Treiber höchstens dreimal innerhalb einer Minute neu
  und fällt danach auf den nächsten Treiber der Kette zurück. Jeder Absturz ist ein
  Exponat: Der Datensatz des Treiberprozesses (Ausnahme, Adresse, Register) bleibt in
  `/now/tasks` stehen, bis `devmgr` ihn abräumt, und eine Zeile landet in
  `/log/drivers.log`.

## Treiberpakete und Treibermodule

Bei manchen Geräteklassen gehört ein Teil des Treibers in den Prozess des Clients. Bei GPUs
ist das der **Vulkan-ICD**: Shader-Compiler und Command-Stream-Aufbau (Mesa). Dieser Teil
muss zur Hardware und zur Protokollversion des Treibers passen. Deshalb liefert ihn das
Treiberpaket mit, als **Treibermodul**.

Treibermodule sind die einzige Ausnahme vom statischen Linken. Dafür gelten strenge
Regeln, damit keine DLL-Hölle entsteht:

1. **Genau eine erlaubte Abhängigkeit:** die System-libc (`libc.so`, musl-Port), deren ABI
   denselben Stabilitätsregeln folgt wie die Syscalls. Alles andere linkt das Modul
   statisch ein, bei Mesa etwa LLVM-frei mit dem eingebauten NIR-Compiler. Der Starter
   prüft `DT_NEEDED` und lehnt alles andere ab.
2. **Genau eine Schnittstelle:** Ein Modul exportiert nur die Einstiegspunkte seiner
   Modul-ABI. Bei Vulkan sind das `vk_icdNegotiateLoaderICDInterfaceVersion` und
   `vk_icdGetInstanceProcAddr`, also die stabile, versionierte Schnittstelle zwischen
   Vulkan-Loader und ICD.
3. **Auswahl über den Treiber, nicht über Suchpfade:** Der Client fragt den GPU-Treiber mit
   `gpu.query(MODULE)` und bekommt ein VMO mit dem Modul zurück, das zu genau diesem Treiber
   gehört. Treiber und Modul kommen immer aus demselben Paket, eine Mischung der
   Versionen ist also ausgeschlossen.
4. **Nur der Gast lädt Module.** Nur Prozesse mit `libc.so` können Module laden, und
   `libc.so` gibt es nur im Gast. Die Mikro-Welt bleibt vollständig statisch und ohne
   libc. In der Praxis betrifft das Chromiums GPU-Prozess.

## Treiber für Phase 1

| Treiber | Klasse | Umgebung | Anmerkung |
|---|---|---|---|
| `uart16550` | serial | QEMU, echte PCs | Diagnose |
| `efifb` | display | überall | Firmware-Framebuffer (GOP) |
| `ps2` | input | QEMU, viele Laptops (interne Tastatur, Touchpad als PS/2-Maus) | |
| `pci` | Bus | überall | ECAM über MCFG, MSI/MSI-X |
| `virtio-blk`, `virtio-net`, `virtio-input`, `virtio-gpu` | diverse | QEMU | Entwicklung und CI |
| `xhci`, `usb-hid`, `usb-storage` | Bus, input, block | echte PCs | |
| `ahci`, `nvme` | block | echte PCs | |
| `e1000e`, `r8169` | net | häufigste LAN-Chips in älteren Laptops und PCs | WLAN erst später |
| `intel-igpu` | display, gpu | Gen9–Gen12 | [05](05-grafik.md) |
| `i2c-hid` | input | neuere Touchpads | braucht ACPI-AML, daher später |
