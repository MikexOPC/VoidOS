# Validazione — Obiettivo 6 & 6B: GUI di VoidOS su Slint 1.19

## Stato
**Fatta** (sull'immagine di produzione `ordinary` in QEMU; hardware fisico non ancora provato).

- `kernel.elf` SHA-256: `0FFC9084D46C14730AB3702D592FF639E77939FC1323F2253AC96EF80C1B7BC5`
- `/bin/ui.elf` (`user-ui.elf`, Slint 1.19 `no_std` + software renderer, `GPL-3.0-only`):
  - SHA-256: `AAC21418DAB61BF3B1981AEEA22DE493B5C975F6A439C9A2786C9FE7A3991C57`
  - Dimensione: **443.504 byte** (~433 KiB) con `opt-level="z"`, LTO e strip
- Volume VirtIO-blk FAT32: 16 MiB (32.768 settori) contenente `/bin/{init,bridge,worker,sh,ui,terminal,sysmon}.elf` e `/ETC/UI/THEME.VSS`.
- Loader ELF utente: 4 Page Table consecutive (`0x400000..0xC00000`, tetto immagine 6 MiB = 1536 pagine, stack utente 64 KiB con guard page).

---

## 1. Test host (Meson `voidos-host-tests`: 7/7 PASS)

| Suite | Risultato | Cosa verifica |
|---|---|---|
| `gui_host` (C + ASan) | **PASS** | Ownership esclusiva display, bounds/overflow rect, input ring PS/2, crescita dinamica SHM, refcounting, rilascio automatico, non-escalation `BUDGET_FLAG_DISPLAY`. |
| `libvoid_rust` | **4/4 PASS** | ABI struct GUI (`DisplayInfo`/`DisplayAcquireOut`/`DisplayRect`/`InputEvent`), allocatore `alloc` Ring 3 (`HeapCore` alloc/dealloc/alignment/coalescing + OOM), token splitting, formattazione. |
| `libui_rust` | **4/4 PASS** | Animazioni fixed-point, parser mini-CSS `.vss`, renderer 2D + `DirtyList`, protocollo IPC `GuiMessage`. |
| `process_table_host` (C + ASan) | **PASS** | Tabella processi dinamica, IPC, non-escalation budget, zero leak heap. |
| `process_policy_rust` | **14/14 PASS** | Policy ELF fino a 1536 pagine, range `0x400000..0xC00000`, IPC, return frames. |
| `elf_parser_rust` | **9/9 PASS** | Parser ELF64 e mutazioni header/segmenti. |
| `security_random_host` (C + ASan) | **PASS** | Entropia, RCT/APT, `HMAC_DRBG`, canary `/GS`. |

---

## 2. Prove sull'immagine di produzione in QEMU

- **Campagna:** `qemu_runner.exe --image kernel/build/meson-kernel/kernel.elf --ordinary-shell --smp 4 --boots 10`
- **Risultato:** **10/10 PASS** (`kernel/build/qemu-runner-18eab3e45e58763c-c124`).
- **Comportamento reale verificato su ogni boot:**
  1. `/bin/ui.elf` (Slint 1.19 `no_std`) alloca il proprio heap `alloc` da 4 MiB su `SYS_SHM_CREATE` + `SYS_SHM_MAP` (`RW+NX`).
  2. Acquisisce il display con `BUDGET_FLAG_DISPLAY`, mappa il backbuffer SHM (768 pagine a 1024×768×32), inizializza `VoidPlatform` + `MinimalSoftwareWindow`.
  3. Renderizza `desktop.slint` e tutti i componenti modulari (`background/`, `window/`, `dock/default/`, `overlay/`, `cursor/`), esegue il morph animato di `DefaultDock` (`0 -> 1`), presenta i dirty rect via `SYS_DISPLAY_PRESENT`, stampa `USER: VoidOS slint compositor ok` ed esce rilasciando display e SHM (zero perdite fisiche confermate dal watchdog HPET + ripristino console testuale).
  4. `/bin/terminal.elf` e `/bin/sysmon.elf` girano concorrentemente in sandbox senza `BUDGET_FLAG_DISPLAY`, confermano isolamento e zero leak.

---

## 3. Licenze e limiti noti

- **Licenza Slint:** `userspace/gui/ui/` usa Slint sotto **`GPL-3.0-only`** (`userspace/gui/ui/LICENSE-GPL-3.0.txt`, sorgenti pubblici sul repo; nessun badge richiesto nella GUI o nella `.iso`).
- **Limiti noti:** rendering CPU software (niente GPU 3D); input PS/2 (USB xHCI in Obiettivo 7); hardware fisico non ancora provato; budget CPU per singolo turno legato a meta periodo del timer ACPI PM 24-bit (`2000 ms < 2343 ms`).
