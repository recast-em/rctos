# 05 – Grafik

## Überblick

```
┌──────────────────── Gast: Chromium (surf) ───────────────────────┐
│ Einbetter:   Fensterverwaltung auf Aura, Eingabe über Ozone       │
│ GPU-Prozess:     Viz → Skia (Vulkan) → ANGLE (WebGL über Vulkan)  │
│                  Ozone-Plattform „rctos“                         │
│                  Treibermodul: Mesa-Vulkan-ICD (z. B. anv)        │
└───────┬───────────────────────────────────────┬──────────────────┘
        │ gpu-Protokoll                          │ display-Protokoll
┌───────▼───────────────────────────────────────▼──────────────────┐
│ intel-igpu (Treiberprozess)                                       │
│  GPU-Teil: GGTT/PPGTT, Execlists, Kontexte, Fences                │
│  Anzeige-Teil: Planes, Page-Flip, VBlank, später Modesetting      │
└───────┬──────────────────────────────────────────────────────────┘
        │ MMIO (BAR0), Apertur (BAR2), MSI, BTI
     Hardware
```

Die Grundsätze:

- **Es gibt keinen Fenster-Server und keinen Compositor-Prozess.** Chromium setzt das Bild
  selbst zusammen und übergibt ganze Frames an die Anzeige.
- **Ein Treiber, zwei Protokolle.** Render-Engine und Display-Engine der iGPU liegen am
  selben PCI-Gerät, also bietet ein Treiberprozess beide Klassen an. Bei getrennter Hardware,
  etwa einem ARM-SoC mit eigenem Display-Controller, sind es zwei Treiber.
- **Nur Vulkan.** Chromium nutzt Vulkan für Skia und ANGLE-auf-Vulkan für WebGL, genau wie
  auf Fuchsia. Einen OpenGL-Treiber wie Mesa `iris` braucht es nicht.

## Zwei Welten auf einem Bildschirm

Der Bildschirm gehört der Mikro-Welt; der Gast bekommt ihn geliehen.

| Wer | Wie er zeichnet | Wann |
|---|---|---|
| Kernel | Boot-Konsole direkt in den Firmware-Framebuffer (Zellen 8 × 8, CP437) | beim Boot und bei einer Panik |
| `console` (Bodenkonsole) | Zellen per `map_framebuffer` in den geleasten Scanout-Speicher, nur geänderte Zellen | immer, wenn niemand sonst die Lease hat |
| `login` | Vollbild-Oberfläche, ebenfalls Zellen | Anmeldung, Sperre, sichere Tastenkombination |
| Programme der Mikro-Welt | direkt in den Framebuffer (`framebuffer.map`) | geliehen von der Konsole |
| Gast | Vulkan, `present` auf seine Grafikpuffer | solange er die Lease hält |

- **Die Lease ist ein Handle, kein Zustand im Treiber.** Endet der Gast oder stürzt er ab,
  verfällt die Lease, und der Anzeige-Teil gibt den Bildschirm an die Konsole zurück. Die
  Konsole zeichnet dann ihre Zellen neu; weil sie keinen Rückspeicher hat, beginnt sie mit
  leerem Bildschirm.
- **Die sichere Tastenkombination** lässt der Eingabedienst nie an den Gast durch. Sie
  widerruft die Lease des Gasts vorübergehend und gibt den Bildschirm an `login`.
- **Die Zellenkonsole ist ein C-Modul** (`con.c`), das Kernel und `console` gemeinsam
  benutzen: dieselbe Schrift, dieselbe Palette, dieselben Regeln.

## Grafikpuffer

Ein Grafikpuffer ist ein **VMO plus Beschreibung**:

```c
typedef struct {
    uint32_t size;          /* Struct-Größe                               */
    uint32_t width, height;
    uint32_t format;        /* FourCC, z. B. XR24                         */
    uint64_t modifier;      /* Tiling/Kompression, DRM-kompatible Werte   */
    uint32_t planes;
    uint32_t stride[4];
    uint64_t offset[4];
} rc_buffer_desc_t;
```

- **Anlegen** übernimmt der GPU-Treiber mit `gpu.buffer_create`, weil er Cache-Attribute,
  Ausrichtung und bei dGPUs später den VRAM-Ort kennt.
- **Teilen** geschieht durch Übertragen des VMO-Handles über einen Kanal. Der
  Empfänger, etwa der Anzeige-Teil, importiert es mit Beschreibung. Das entspricht dmabuf
  unter Linux, nur als gewöhnliches Kernel-Objekt.
- Für die **Modifier** gelten dieselben Werte wie bei DRM (`DRM_FORMAT_MOD_*`). Mesa und
  Chromium kennen sie bereits, man muss nichts übersetzen.

## Synchronisation

Ein **Fence** ist ein gewöhnliches `Event`-Objekt. Der Treiber setzt `SIGNALED`, sobald die
GPU die Arbeit abgeschlossen hat. Das erfährt er über den Interrupt und eine
Breadcrumb-Sequenznummer. Wer wartet, bindet das Event an einen Port. Vulkan-Timeline-Semaphore
emuliert Mesa über binäre Syncs, dafür hat es eine gemeinsame Emulation (`vk_sync_timeline`).
Ein neuer Kernel-Objekttyp ist deshalb nicht nötig.

## Anzeige-Protokoll

| Nachricht | Zweck |
|---|---|
| `list` | Anzeigen, Modi, aktueller Modus, EDID |
| `lease(display)` | exklusive Übernahme; liefert Lease-Handle (EventPair) |
| `release(lease)` | Rückgabe |
| `map_framebuffer(lease)` | Ausschnitt des Scanout-Speichers für Direktzugriff ([03](03-rechte.md#direktzugriff)) |
| `buffer_import(lease, vmo, desc)` → `buf_id` | Puffer für Scanout registrieren |
| `present(lease, buf_id, wait_event, flags)` | beim nächsten VBlank zeigen; Ereignis `presented(buf_id, timestamp)` |
| `set_mode(lease, mode)` | Modus setzen (ab Stufe D2) |
| Ereignisse `vblank`, `hotplug`, `revoked` | – |
| `planes`, `present_planes(…)` | Overlays (ab Stufe D3) |

## GPU-Protokoll

Das Protokoll ist bewusst schmal und orientiert sich an Fuchsias Magma und an der
`xe`-Schnittstelle des Linux-Kernels:

| Nachricht | Zweck |
|---|---|
| `query(param)` | Geräte-ID, Revision, Engines, GTT-Größe, Timestamp-Frequenz, `MODULE` (liefert VMO des Treibermoduls) |
| `context_create(flags)` → `ctx` | eigener GPU-Adressraum (PPGTT) pro Kontext |
| `context_destroy(ctx)` | – |
| `buffer_create(size, flags)` → `buf_id`, VMO | – |
| `buffer_import(vmo)` → `buf_id` | – |
| `buffer_release(buf_id)` | – |
| `vm_bind(ctx, buf_id, offset, len, gpu_va, flags)` | Puffer in den GPU-Adressraum einblenden |
| `vm_unbind(ctx, gpu_va, len)` | – |
| `exec(ctx, engine, batch_va, wait_events[], signal_event)` | Batch-Buffer einreichen; Events als Handles |
| Ereignisse `context_lost`, `fault(gpu_va)` | Reset bzw. GPU-Seitenfehler |

Die Nutzlast von `exec` ist ein Batch-Buffer, den das Treibermodul im GPU-eigenen Befehlsformat
gebaut hat. Der Treiber prüft ihn nicht inhaltlich. Die Trennung sichert der eigene
PPGTT-Adressraum jedes Kontexts ab: Ein Batch sieht nur Puffer, die sein Kontext
eingeblendet hat. So macht es auch Linux ab Gen8 mit Full-PPGTT.

## Der Treiber `intel-igpu`

### Zielhardware

| Generation | Plattformen (Auswahl) | Jahre | Status |
|---|---|---|---|
| Gen9 / Gen9.5 | Skylake, Kaby Lake, Coffee Lake, Comet Lake, Gemini Lake | 2015–2020 | **Phase 1** |
| Gen11 | Ice Lake, Jasper Lake, Elkhart Lake | 2019–2021 | Phase 1 |
| Gen12 (Xe-LP) | Tiger Lake, Rocket Lake, Alder Lake-S, Raptor Lake-S | 2020–2023 | Phase 1b |
| Gen7.5 / Gen8 | Haswell, Broadwell | 2013–2015 | später, Mesa `hasvk` statt `anv` |
| Alder Lake-P und neuer, Meteor Lake, Arc | – | ab 2022 | später, weil GuC-Submission vorgesehen ist |

Das deckt einen großen Teil der ausgemusterten Business-Laptops und Büro-PCs ab.

### Ohne Firmware-Blob lauffähig

Auf Gen9 bis Gen12 lassen sich Befehle über **Execlists** einreichen, also direkt über
Register und Kontext-Deskriptoren. Die GuC-Firmware ist dafür nicht nötig. HuC betrifft nur
Video, DMC nur tiefe Energiesparzustände des Display-Teils. Damit läuft der ganze Pfad von
Phase 1 **ohne geladene Firmware**. DMC kommt später für den Akkubetrieb dazu.

### Aufbau

1. **Übernahme:** PCI-BARs einblenden, Bus-Master einschalten, MSI einrichten, Forcewake
   anfordern.
2. **GGTT:** Die globale GTT liegt in der oberen Hälfte von BAR0. Die Einträge, die die
   Firmware für den GOP-Framebuffer gesetzt hat, bleiben erhalten.
3. **PPGTT:** 4-stufige Seitentabellen pro Kontext. `vm_bind` trägt dort ein.
4. **Engines:** Zuerst die Render-Engine (RCS), danach Blitter (BCS). Video-Engines erst
   später.
5. **Kontexte:** Logical Ring Contexts (LRC). Das Kontextbild wird mit einer
   Grundkonfiguration initialisiert, einem „goldenen Kontext“.
6. **Einreichen:** Deskriptoren in den ELSP bzw. die ELSQ schreiben; Context-Switch-Interrupts
   und Context Status Buffer auswerten.
7. **Fertigmeldung:** `MI_STORE_DATA_IMM` bzw. `PIPE_CONTROL` schreibt am Ende jedes Batches
   eine Sequenznummer (Breadcrumb) und löst einen Interrupt aus. Der Treiber signalisiert das
   passende Event.
8. **Workarounds:** Pro Plattform müssen Register-Workarounds gesetzt werden. Das ist
   erfahrungsgemäß der mühsamste Teil. Die Listen kommen aus `i915` bzw. `xe` in Linux
   (`intel_workarounds.c`, `xe_wa.c`).
9. **Reset:** Bei einem Hang wird die Engine zurückgesetzt und dem betroffenen Kontext
   `context_lost` gemeldet.

### Referenzen

- **Fuchsia `msd-intel-gen`:** ein kompakter Gen9-Treiber als Userspace-Prozess mit
  Magma-Protokoll, BSD-lizenziert. Er ist konzeptionell die beste Vorlage.
- **Linux `i915`/`xe`:** vollständig, aber sehr groß. Er dient als Nachschlagewerk für
  Registerfolgen und Workarounds.
- **Intel Graphics PRMs:** öffentliche Programmierhandbücher für jede Generation.

### Stufen der Anzeige

| Stufe | Inhalt | Ergebnis |
|---|---|---|
| **D0** | `efifb`: GOP-Framebuffer flach eingeblendet, ein Puffer, CPU kopiert | Bild auf jedem UEFI-Gerät, ohne GPU-Treiber |
| **D1** | **Page-Flip ohne Modesetting:** Die Einstellungen der Firmware (Pipe, Port, Takt, Modus) bleiben, der Treiber schreibt nur die Pufferadresse der primären Plane (`PLANE_SURF`) neu. Die Hardware übernimmt sie beim nächsten VBlank. Dazu kommen VBlank- und Flip-Done-Interrupts. | Doppelpuffer ohne Tearing, keine Kopie, geringes Risiko |
| **D2** | Echtes Modesetting: DDI, PLLs, Link-Training für eDP/DP, HDMI, EDID über AUX/GMBUS, Hotplug, mehrere Anzeigen | externe Monitore, Auflösungswechsel |
| **D3** | Mehrere Planes, gekachelte bzw. komprimierte Scanout-Formate | Chromium-Overlays (Video), weniger Speicherbandbreite |

Stufe D1 trägt Phase 1. Der Aufwand ist klein, die Wirkung groß, und die schwierige
Display-Initialisierung hat bereits die Firmware erledigt.

## Mesa-Anbindung

Mesa hat für seine Vulkan-Treiber inzwischen fast überall eine Abstraktion der
Kernel-Schnittstelle. An diese Stelle kommt jeweils ein rctos-Backend:

| Mesa-Treiber | Hardware | Abstraktion |
|---|---|---|
| `anv` | Intel Gen9+ | `anv_kmd_backend` (heute i915 und xe) |
| `hasvk` | Intel Gen7/8 | direkt i915, braucht Anpassung |
| `radv` | AMD | `radeon_winsys` |
| `turnip` | Qualcomm Adreno | `tu_knl` (msm, kgsl, virtio) |
| `panvk` | ARM Mali | `pan_kmod` |
| `v3dv` | Broadcom V3D (Raspberry Pi) | direkt DRM-v3d, braucht Anpassung |
| `venus` | virtio-gpu (QEMU) | virtio-gpu-Schnittstelle |

Für `anv` sind folgende Arbeiten nötig:

1. **`anv_kmd_backend` für rctos:** Die Funktionen für Puffer anlegen und einblenden,
   `vm_bind`, `exec` und Gerätedaten abfragen werden auf das GPU-Protokoll abgebildet.
2. **Gerätedaten:** `intel_device_info` wird aus `gpu.query` statt aus i915-Queries befüllt.
3. **Externe Speicher und Semaphoren:** Eine eigene Erweiterung nach dem Vorbild von
   `VK_FUCHSIA_external_memory` und `VK_FUCHSIA_external_semaphore`, mit VMO- und
   Event-Handles statt Dateideskriptoren. Chromium und ANGLE haben für die
   Fuchsia-Erweiterungen bereits Code, der sich übertragen lässt.
4. **WSI:** Ein Mesa-WSI-Backend `wsi_common_rctos.c` präsentiert über das
   Anzeige-Protokoll. Das ist der einfache erste Weg für Chromium (siehe unten) und für
   Vulkan-Demos.
5. **Build:** Mesa wird mit Meson gegen den musl-Port gebaut, mit `-Dvulkan-drivers=intel`,
   ohne Gallium und ohne LLVM. Das Ergebnis ist das Treibermodul
   `vulkan_intel.so` ([04](04-treibermodell.md#treiberpakete-und-treibermodule)).

## Anbindung an Chromium

Die Einzelheiten stehen in [06](06-chromium.md#ozone-plattform-rctos). Die Grafikstufen
bauen aufeinander auf:

| Stufe | Pfad | Voraussetzung |
|---|---|---|
| **G0** | Software: Skia rendert auf der CPU, Ozone kopiert in den Framebuffer (`map_framebuffer`) | nur `efifb` |
| **G1** | Vulkan mit Swapchain: Skia/Viz rendern per Vulkan, Präsentation über `VK_RCTOS_surface` (Mesa-WSI) | GPU-Treiber, Treibermodul, D0 oder D1 |
| **G2** | Zero-Copy: Chromium verwaltet Grafikpuffer selbst (`NativePixmap`), präsentiert direkt über `present` und nutzt Overlays | D1 bzw. D3, externe Speicher-Erweiterung |

In QEMU wird G1 ohne echte Hardware mit **`virtio-gpu` und Venus** erprobt. Venus reicht
Vulkan-Aufrufe an den Host weiter, der Gast braucht dazu nur `venus` als Treibermodul.

## Rückfall und Robustheit

- **Kein GPU-Treiber:** Anzeige über `efifb`, Chromium mit SwiftShader oder Software-Compositing.
- **GPU-Absturz:** Der Treiber setzt die Engine zurück und meldet `context_lost`. Chromium
  kann seinen GPU-Prozess neu aufbauen, dafür hat es bereits Mechanismen.
- **Treiber-Absturz:** `devmgr` startet ihn neu. Die Anzeige bleibt dabei stehen, weil die
  Display-Engine das letzte Bild weiter zeigt, und Chromium verbindet sich neu.

## Ausblick ARM

Auf ARM-SoCs sind Display-Controller und GPU getrennte Blöcke, etwa HVS/PixelValve und V3D
beim Raspberry Pi oder VOP2 und Mali beim RK3588. Es gibt dann zwei Treiber, einen für
`display` und einen für `gpu`. Grafikpuffer werden als VMO zwischen ihnen geteilt, wie oben
beschrieben. Die Protokolle ändern sich nicht. Deshalb darf auf x86-64 nichts in die
Protokolle einsickern, was nur für die Intel-iGPU gilt.

## Alternative: GPU-Treiber vollständig im Treiberprozess

Die Venus-Architektur ließe sich auch auf echte Hardware übertragen. Chromium hätte dann nur
einen generischen Venus-Client, und Mesa (`anv` usw.) liefe im Treiberprozess. Vorteile:
kein Treibermodul im Client, stärkere Isolation und einfacherer Austausch. Nachteile: mehr
Latenz pro Aufruf, mehr Kopien und mehr Komplexität im Treiber. Der Ansatz wird erst
geprüft, wenn G1 läuft und sich messen lässt.
