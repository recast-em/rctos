# 03 – Rechte

## Grundsatz

Ein Prozess darf genau das, wofür er ein Handle hat. Es gibt keinen Root-Benutzer, keine
Benutzer-IDs im Kernel und keinen globalen Pfad- oder Namensraum im Kernel. Wer ein Gerät
ansprechen will, braucht einen Kanal zu dessen Treiber. Wer Hardware direkt anfassen will,
braucht ein Ressourcen-Handle.

Rechte lassen sich nur **weitergeben** (mit `TRANSFER`) oder **verringern** (mit
`sys_handle_duplicate` oder `sys_handle_replace`), aber nie vergrößern.

## Handle-Rechte

| Recht | Bit | Bedeutung |
|---|---|---|
| `DUPLICATE` | 0 | Handle darf dupliziert werden |
| `TRANSFER` | 1 | Handle darf über einen Kanal verschickt werden |
| `READ` | 2 | lesen (VMO, Kanal, Log) |
| `WRITE` | 3 | schreiben (VMO, Kanal, Log) |
| `EXECUTE` | 4 | VMO darf ausführbar eingeblendet werden |
| `MAP` | 5 | VMO darf eingeblendet werden |
| `WAIT` | 6 | auf Signale warten, an Port binden |
| `SIGNAL` | 7 | Benutzersignale am Objekt setzen |
| `SIGNAL_PEER` | 8 | Benutzersignale an der Gegenseite setzen |
| `INSPECT` | 9 | `sys_object_info` |
| `MANAGE` | 10 | verwalten: Threads im Prozess anlegen, Priorität setzen, Größe ändern, Ausschnitte widerrufen, Cache-Attribut setzen |
| `KILL` | 11 | Prozess oder Thread beenden |

### Vorgaben je Typ

| Typ | Rechte bei Erzeugung |
|---|---|
| `Process` | DUPLICATE, TRANSFER, WAIT, INSPECT, MANAGE, KILL |
| `Thread` | DUPLICATE, TRANSFER, WAIT, INSPECT, MANAGE, KILL |
| `AddressRegion` | DUPLICATE, TRANSFER, READ, WRITE, MAP, INSPECT, MANAGE (EXECUTE nur, wenn der Elternbereich es hat) |
| `Vmo` | DUPLICATE, TRANSFER, READ, WRITE, MAP, WAIT, INSPECT, MANAGE; **nie** EXECUTE |
| `Channel` | TRANSFER, READ, WRITE, WAIT, SIGNAL, SIGNAL_PEER, INSPECT (kein DUPLICATE, weil ein Kanal genau einen Besitzer hat) |
| `Event`, `EventPair`, `Timer` | DUPLICATE, TRANSFER, WAIT, SIGNAL (EventPair zusätzlich SIGNAL_PEER), INSPECT |
| `Port` | DUPLICATE, TRANSFER, READ, WRITE, WAIT, INSPECT |
| `Resource`, `Msi`, `Bti` | DUPLICATE, TRANSFER, INSPECT |
| `Interrupt` | TRANSFER, WAIT, INSPECT |

## Ressourcen

Hardware-Zugriff ist in einem Baum von `Resource`-Objekten organisiert. Nur `init` bekommt
die Wurzel. Jeder Knoten kann mit `drv_resource_create` Teilbereiche abspalten, aber nie
über den eigenen Bereich hinaus.

| Art | Bereich | Erlaubt |
|---|---|---|
| `ROOT` | alles | Teilressourcen jeder Art anlegen |
| `MMIO` | physischer Adressbereich | `drv_vmo_create_physical` |
| `IOPORT` | Portbereich (x86-64) | `drv_ioport_enable` |
| `IRQ` | GSI/SPI-Bereich bzw. Anzahl von MSI-Vektoren | `drv_interrupt_create`, `drv_msi_allocate` |
| `IOMMU` | IOMMU-Einheit und Bus-ID-Bereich | `drv_bti_create` |
| `EXEC` | – | `sys_vmo_make_executable` (JIT) |
| `SMC` | Funktionsnummern-Bereich (AArch64) | `drv_smc_call` |
| `POWER` | – | `drv_system_power` |

Beispiel für die Verteilung beim Boot:

```
ROOT (init)
 ├─ MMIO 0x0–max, IOPORT 0–0xFFFF, IRQ alle, IOMMU alle ─► devmgr
 │     ├─ MMIO BAR0+BAR2 der iGPU, IRQ 1 MSI, IOMMU 00:02.0 ─► intel-igpu
 │     ├─ IOPORT 0x3F8–0x3FF, IRQ 4                            ─► uart16550
 │     └─ MMIO ECAM, IRQ MSI-Pool                               ─► pci
 ├─ EXEC  ─► Starter (vergibt es an Prozesse mit Berechtigung „jit“)
 └─ POWER ─► Energiedienst
```

`init` behält nach dem Verteilen keine Hardware-Rechte.

## Manifeste und Berechtigungen

Jedes Programm hat ein **Manifest**, eine Textdatei neben der Binärdatei im Paket. Der
Starter liest es und übergibt beim Start genau die Handles, die dort verlangt werden und
die die **Systemrichtlinie** des Profils erlaubt. Was nicht im Manifest steht, bekommt der
Prozess nicht.

```ini
# /pkg/chromium/manifest
name        = chromium
binary      = bin/chromium
priority    = 12
max_priority= 20
memory      = 3G            # Kontingent
handles     = 65536

[use]
service     = fs, net, display, input, gpu, audio, fonts
permission  = jit, display.lease, input.focus, process.spawn
```

| Berechtigung | Wirkung |
|---|---|
| `jit` | Prozess bekommt ein `EXEC`-Ressourcen-Handle |
| `process.spawn` | Prozess darf Kindprozesse anlegen (Kontingent wird geteilt) |
| `display.lease` | Prozess darf eine Anzeige exklusiv übernehmen |
| `framebuffer.map` | Prozess darf den Framebuffer der Lease direkt einblenden |
| `input.focus` / `input.grab` | Eingaben empfangen bzw. exklusiv erhalten |
| `ioport:<von>-<bis>` | direkter IO-Port-Zugriff (nur x86-64, nur für Treiber und Diagnose) |
| `realtime` | Prioritäten 24–31 |

Treiber brauchen keine eigenen Berechtigungen dieser Art. Ihre Ressourcen bekommen sie vom
`devmgr` anhand des Treiber-Manifests ([04](04-treibermodell.md)).

## Direktzugriff

Manche Programme sollen die Hardware ohne Umweg benutzen dürfen: ein Vollbildspiel, ein
Kiosk-Programm, eine Embedded-Anzeige oder ein Diagnosewerkzeug. Das ist erlaubt, wenn es
**ausdrücklich vergeben** und **widerrufbar** ist.

### Flach gemappter Framebuffer

1. Die Anwendung ruft `usr_display_lease(display_id, …)` auf. Der Display-Treiber prüft
   die Berechtigung `display.lease`, die ihm der Starter als Kennzeichen mitgibt, und
   vergibt die Lease.
2. Die Anwendung ruft `usr_display_map_framebuffer(lease, &info, &slice)` auf. Der Treiber
   erzeugt mit `sys_vmo_create_slice` einen **Ausschnitt** des Scanout-Speichers: physischer
   Speicher mit Cache-Attribut `WC` bzw. einen Grafikpuffer. Nur diesen Ausschnitt
   überträgt er an die Anwendung.
3. Die Anwendung blendet den Ausschnitt mit `sys_vm_map` ein und schreibt direkt linear in
   den Bildspeicher. `info` enthält Breite, Höhe, Stride und Format.
4. Muss die Anzeige zurückgenommen werden, etwa für Sperrbildschirm, Notfallanzeige oder
   einen Absturz der Anwendung, ruft der Treiber `sys_vmo_revoke(slice)` auf. Alle
   Einblendungen verschwinden sofort. Ein weiterer Zugriff löst einen Seitenfehler aus,
   den die Anwendung als Ereignis `LEASE_REVOKED` sieht.

Im Profil `embedded` kann so ein einzelnes Programm ohne GPU-Treiber, Compositor oder
Grafikbibliothek einen Bildschirm vollständig ansteuern.

### IO-Ports und MMIO

`drv_ioport_enable` trägt auf x86-64 den Portbereich in die IO-Berechtigungsbitmap des
Prozesses ein. Die Bitmap (8 KiB) wird nur für Prozesse angelegt, die sie brauchen. MMIO
funktioniert über physische VMOs. Für beides braucht man eine Ressource, die man nur über
das Manifest bekommt.

### Exklusive Eingabe

`usr_input_grab` leitet alle Eingaben an die Anwendung, solange sie die Lease hält.
Ausgenommen ist die **sichere Tastenkombination**: Sie geht immer an `init` und nimmt
Lease und Grab zurück. Welche Kombination das ist, legt das Profil fest; Vorgabe ist
Strg+Alt+Entf.

## Sicherheitsgrenzen

- **DMA ohne IOMMU.** Ohne IOMMU (VT-d bzw. SMMU) kann jeder Treiber, der ein Gerät zu DMA
  anweisen darf, den gesamten Speicher lesen und schreiben. Solche Treiber sind dann
  faktisch vertrauenswürdig. `drv_bti_create` gibt es auch ohne IOMMU, liefert dann aber
  Bus-Adressen gleich physischen Adressen. `devmgr` protokolliert diesen Zustand, und die
  Richtlinie kann ihn verbieten.
- **Meltdown und KPTI.** Viele ältere Intel-CPUs der Zielgruppe sind anfällig. Der Kernel
  prüft `IA32_ARCH_CAPABILITIES.RDCL_NO` und schaltet bei Bedarf getrennte Seitentabellen
  für Kernel und Benutzer ein (KPTI). Mit PCID bleibt der Aufwand gering. KPTI kostet eine
  zusätzliche Seitentabellen-Wurzel pro Prozess, das steht im Budget.
- **Spectre.** Bei Kontextwechseln zwischen Prozessen werden die Branch-Prädiktoren
  geleert (IBPB, sofern vorhanden). Bei Bedarf kommen Retpolines oder IBRS hinzu. Dazu
  kommen SMEP und SMAP (x86-64) bzw. PXN und PAN (AArch64, soweit vorhanden).
- **SYSRET-Falle (x86-64).** Vor `sysret` prüft der Kernel, ob die Rücksprungadresse
  kanonisch ist. Ist sie es nicht, kehrt er über `iret` zurück.
- **Chromium-Sandbox.** Ein Renderer bekommt nur seinen Mojo-Kanal zum Browser-Prozess,
  eine `EXEC`-Ressource für V8 und sein Kontingent. Er hat keinen Namensraum, keinen
  Dateizugriff und kein Netz. Auf rctos ist das kein Filter über einem mächtigen System
  wie seccomp auf Linux, sondern die schlichte Abwesenheit von Handles.
