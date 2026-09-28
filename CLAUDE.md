# Hinweise für die Arbeit an rctos

- Dokumente auf Deutsch, Bezeichner im Code auf Englisch.
- **Sprache: C11 (Kernel freestanding), Assembler nur in `kernel/arch/`. Kein Rust** – ausdrücklicher Wunsch des Projektinhabers. Rust nicht vorschlagen und nicht einführen; einzige geduldete Ausnahme sind Rust-Komponenten, die Chromium selbst mitbringt (siehe `docs/06-chromium.md`).
- **Leitbild Mikro-Umgebung** (`docs/00-vision.md`): klein, gemessen, inspizierbar, explizit statt automatisch. Chromium ist der Gast und die eine Ausnahme; nichts in der Mikro-Welt entsteht nur für ihn. Die Mikro-Welt braucht keine libc, POSIX gibt es nur im Gast.
- Kein Overengineering: flache C-APIs, statisch gelinkt, keine Zwischenschichten „für später“.
- Budgets aus `kernel/core/budget.h` und `docs/00-vision.md` sind verbindlich (Kernel ≤ 1 MiB, ≤ 10.000 Zeilen, Profil `minimal` ≤ 512 KiB RAM); beide Stellen ändern sich nur gemeinsam.
- Zeichenketten, die Bildschirm oder serielle Schnittstelle erreichen: ASCII, Meldungen englisch; CP437-Zeichen oberhalb 0x7F als `\xNN`.
- Vor jedem Push: `make` (baut mit `-Werror`, prüft das Budget) und `make test` (bootet in QEMU mit UEFI und BIOS, verlangt `gate ok`).
- Architektur steht in `docs/`; Änderungen am Design dort mitpflegen, Entscheidungen mit Datum in `docs/00-vision.md` eintragen. Familie RCP-OS: gemeinsame Konventionen nicht ohne Grund brechen.
