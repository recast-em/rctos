# Hinweise für die Arbeit an rctos

- Dokumente auf Deutsch, Bezeichner im Code auf Englisch.
- **Sprache: C11 (Kernel freestanding), Assembler nur in `kernel/arch/`. Kein Rust** – ausdrücklicher Wunsch des Projektinhabers. Rust nicht vorschlagen und nicht einführen; einzige geduldete Ausnahme sind Rust-Komponenten, die Chromium selbst mitbringt (siehe `docs/06-chromium.md`).
- Kein Overengineering: flache C-APIs, statisch gelinkt, keine Zwischenschichten „für später“.
- Budgets aus `docs/00-vision.md` sind verbindlich (Kernel ≤ 1 MiB, Profil `minimal` ≤ 512 KiB RAM).
- Architektur steht in `docs/`; Änderungen am Design dort mitpflegen.
