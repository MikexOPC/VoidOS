# GUI di VoidOS

Architettura a 5 strati in Ring 3 (`Rust no_std`), mediata dal kernel solo per
display, input unificato PS/2 e shared memory `RW+NX`:

- `ui/` — Compositor `/bin/ui.elf` basato su **Slint 1.19 (`no_std` + software renderer, licenza `GPL-3.0-only`)** con heap `alloc` su SHM (`libvoid::heap::ShmAllocator`) e componenti `.slint` modulari in `ui/components/`:
  - `background/background.slint` (Layer 0)
  - `window/window.slint` (Layer 1)
  - `dock/default/dock.slint` (Layer 2 — variante default con 4 forme animate; pronto per affiancare altre varianti in `dock/<variante>/`)
  - `overlay/overlay.slint` (Layer 3)
  - `cursor/cursor.slint` (Layer 4)
- `application/` — Applicazioni utente sandboxed (senza `BUDGET_FLAG_DISPLAY`):
  - `terminal/` -> `/bin/terminal.elf`
  - `sysmon/` -> `/bin/sysmon.elf`
- `libui/` — Libreria condivisa leggera (`anim`, `style`, `draw`, `proto` IPC `#[repr(C)]`) usata dalle app sandboxed.

Documenti di riferimento:
- [Design GUI](../../docs/design/gui.md)
- [Validazione GUI](../../docs/validation/gui.md)
- [Licenza GPLv3 del compositor Slint](ui/LICENSE-GPL-3.0.txt)
