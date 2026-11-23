# GUI di VoidOS

Architettura a 5 strati in Ring 3 (`Rust no_std`), mediata dal kernel solo per
display, input unificato PS/2 e shared memory `RW+NX`:

- `libui/` — Libreria condivisa (`anim`, `style` mini-CSS `/ETC/UI/THEME.VSS`,
  `draw` 2D con blitter `SSE2` + dirty rects + font bitmap, `proto` IPC `#[repr(C)]`).
- `ui/` — Compositor `/bin/ui.elf` e componenti di strato in `ui/components/`:
  - `background/` (Layer 0)
  - `window/` (Layer 1)
  - `dock/` (Layer 2 — forma/posizione dinamica con morph animato)
  - `overlay/` (Layer 3)
  - `cursor/` (Layer 4)
- `application/` — Applicazioni utente sandboxed (senza `BUDGET_FLAG_DISPLAY`):
  - `terminal/` -> `/bin/terminal.elf`
  - `sysmon/` -> `/bin/sysmon.elf`

Documenti di riferimento:
- [Design GUI](../../docs/design/gui.md)
- [Validazione GUI](../../docs/validation/gui.md)
