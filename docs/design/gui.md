# Design — Obiettivo 6: GUI di VoidOS

## Obiettivo
Avviare direttamente l'immagine di produzione in un desktop grafico in Ring 3
composto a strati (sfondo, finestre, dock dinamica, overlay, cursore) con
animazioni fluide, temi in formato mini-CSS su disco (`/etc/ui/theme.vss`) e
due applicazioni isolate in `userspace/gui/application/` (**Terminale** con
`/bin/sh.elf` e **System Monitor** via `/bin/bridge.elf`), mantenendo il
ripristino automatico della console testuale se il compositor esce o va in fault.

---

## 1. Struttura e linguaggi

- **Kernel (`C`):** espone solo le primitive sicure di display, input e memoria
  condivisa. Nessuna logica di finestre, font o widget nel kernel.
- **`userspace/gui/libui/` (`Rust no_std`):** libreria condivisa da compositor
  e applicazioni:
  - `anim`: interpolatori legati al clock reale (ms), easing (`linear`,
    `ease_out`, `ease_in_out`, `spring`), transizioni su posizione, dimensione,
    opacita e colore RGBA.
  - `style`: parser leggero per `/etc/ui/theme.vss` (mini-CSS: colori, padding,
    bordi, radius, edge/forma dock, durate transizioni), compilato in struct
    binarie una volta all'avvio.
  - `draw`: renderer 2D software `BGRX/BGRA` con clipping, rettangoli
    arrotondati, alpha blending, dirty rectangles, font bitmap 8x16 e blitter
    accelerato via `SSE2` (con fallback scalare).
  - `proto`: protocollo IPC binario `#[repr(C)]` neutro rispetto al linguaggio.
- **`userspace/gui/ui/` (`Rust no_std` -> `/bin/ui.elf`):** compositor a strati.
- **`userspace/gui/application/` (`Rust no_std`):**
  - `terminal/` -> `/bin/terminal.elf` (emulatore terminale che avvia `/bin/sh.elf`)
  - `sysmon/` -> `/bin/sysmon.elf` (CPU, RAM libera, processi via `/bin/bridge.elf`)

---

## 2. Architettura a strati del Compositor

Il compositor mantiene 5 layer indipendenti composti dal basso verso l'alto
**solo dentro i dirty rectangles** del frame corrente:

```text
Layer 4: Cursor       cursore mouse software (dirty-rect minimo)
Layer 3: Overlay      menu, tooltip, notifiche
Layer 2: Dock / Bar   dock configurabile (forma, bordo schermo, auto-hide, morph)
Layer 1: Windows      finestre client ordinate per z-order / focus
Layer 0: Background   sfondo / gradiente / colore da theme.vss
```

La **Dock** e un componente di Layer 2 separato da sfondo e finestre: puo
cambiare forma (`floating` pillola, `bar` piena, `compact`) e posizione
(`bottom`, `top`, `left`, `right`) con transizioni animate da `libui::anim`.

---

## 3. Syscall kernel, ownership e sicurezza

Nuove syscall (a partire da `SYS_DISPLAY_INFO = 16`), con validazione al confine:

| Syscall | Chi puo chiamarla | Funzione |
|---|---|---|
| `SYS_DISPLAY_INFO (16)` | Tutti (solo lettura) | Restituisce `width`, `height`, `stride`, `format`, `uptime_ms`. |
| `SYS_DISPLAY_ACQUIRE (17)` | Solo processo con `BUDGET_FLAG_DISPLAY` | Acquisisce il possesso esclusivo del display e mappa il framebuffer/backbuffer condiviso (`SHM` dedicata) nello spazio utente del compositor. |
| `SYS_DISPLAY_PRESENT (18)` | Solo proprietario corrente | Presenta una lista di rettangoli sporchi `(x, y, w, h)` su GOP o VirtIO-GPU, validando bounds e overflow. |
| `SYS_INPUT_POLL (19)` | Solo proprietario corrente | Legge eventi dalla coda kernel unificata (tastiera PS/2 + `KERNEL_PS2_MOUSE_EVENT` delta X/Y e bottoni). |
| `SYS_SHM_CREATE (20)` / `SYS_SHM_MAP (21)` / `SYS_SHM_UNMAP (22)` | Processi autorizzati | Crea e mappa regioni di memoria condivisa (pagine `RW` + `NX`, mai eseguibili) per i buffer pixel delle singole finestre tra app e compositor. |

### Regole di ownership e isolamento
- **Un solo proprietario del display:** al massimo un PID alla volta possiede il
  display (`BUDGET_FLAG_DISPLAY`, concesso da `/bin/init.elf` solo a `/bin/ui.elf`,
  non ereditabile da processi figli con budget ridotto).
- **Isolamento finestre:** ogni finestra usa una `SHM` dedicata mappata solo
  nell'app proprietaria e nel compositor. Un'app non vede mai il framebuffer
  globale ne i buffer di altre app.
- **Crash recovery obbligatorio:**
  - Se un'app GUI crasha, il kernel revoca la sua `SHM` e notifica la chiusura
    del canale IPC; il compositor rimuove la finestra e il desktop resta vivo.
  - Se `/bin/ui.elf` esce o va in fault, il cleanup del processo nel kernel
    rilascia automaticamente il possesso del display e ripristina la console
    testuale + shell senza riavviare il sistema.
- **Compatibilita console/test:** la console seriale resta sempre attiva; se
  non c'e display grafico o se il compositor esce, `/bin/init.elf` mantiene
  operativo il percorso console/shell.

---

## 4. Protocollo IPC Compositor <-> App (`#[repr(C)]`)

Messaggi binari fissi sui canali IPC esistenti (max `IPC_MESSAGE_BYTES`):
- Client -> Compositor: `CreateWindow(w, h, flags, shm_id, title)`,
  `CommitRect(win_id, x, y, w, h)`, `SetTitle(win_id, title)`, `CloseWindow(win_id)`.
- Compositor -> Client: `WindowCreated(win_id, actual_w, actual_h)`,
  `InputKey(win_id, scancode, ascii, mods)`, `InputMouse(win_id, x, y, buttons)`,
  `FocusChanged(win_id, focused)`, `CloseRequested(win_id)`.

---

## 5. Piano di esecuzione (Task 6.1 -> 6.6)

1. **Task 6.1 — Syscall kernel (`display`, `input`, `shm`) e crash recovery:**
   - Implementazione `SYS_DISPLAY_INFO/ACQUIRE/PRESENT`, `SYS_INPUT_POLL`,
     `SYS_SHM_CREATE/MAP/UNMAP`, flag `BUDGET_FLAG_DISPLAY` non escalabile,
     coda eventi unificata tastiera+mouse PS/2, ripristino console all'uscita
     del proprietario.
   - Test host (bounds/overflow/permessi/lifecycle SHM) + 3 boot QEMU.
2. **Task 6.2 — Libreria `userspace/gui/libui/` (`anim`, `style`, `draw`, `proto`):**
   - Motore animazioni con easing, parser mini-CSS `/etc/ui/theme.vss`,
     renderer 2D con blitter `SSE2`, font bitmap 8x16, dirty rects, struct IPC.
   - Test host completi su animazioni, parser `.vss`, clipping e blending.
3. **Task 6.3 — Compositor a strati (`userspace/gui/ui/` -> `/bin/ui.elf`):**
   - 5 layer (`Background`, `Windows`, `Dock`, `Overlay`, `Cursor`), Dock con
     forme/posizioni configurabili e transizioni animate, gestione mouse/focus,
     avvio diretto da `/bin/init.elf` + file `/etc/ui/theme.vss` sul disco FAT32.
   - Verifica pixel/eventi + 3 boot QEMU (incluso crash recovery del compositor).
4. **Task 6.4 — App Terminale (`userspace/gui/application/terminal/`):**
   - Finestra grafica con scrollback, cursore testo, input tastiera inoltrato dal
     compositor e sessione collegata a `/bin/sh.elf` (`fl`, `rp`, `print`, ecc.).
   - Verifica comandi reali dentro la finestra + 3 boot QEMU.
5. **Task 6.5 — App System Monitor (`userspace/gui/application/sysmon/`):**
   - Finestra grafica che interroga `/bin/bridge.elf` per CPU, memoria libera e
     stato processi, con barre animate via `libui::anim`.
   - Test multitasking visivo: Terminale + System Monitor aperti insieme, focus,
     drag, e prova di fault di un'app senza impattare l'altra o il desktop.
6. **Task 6.6 — Validazione finale Obiettivo 6 e aggiornamento `docs/status.md`:**
   - Test host completi, 10 boot consecutivi dell'immagine di produzione in QEMU
     (verifica rendering, input, isolamento, crash recovery), `docs/validation/gui.md`
     e `docs/status.md`.

---

## 6. Limiti noti accettati per v1

- Rendering 100% software su CPU (`SSE2` + dirty rects); accelerazione GPU 3D
  (VirGL / driver reali) rinviata a dopo l'hardware reale.
- Font bitmap 8x16 nitido (niente rasterizzatore TrueType/FreeType in v1).
- Input via PS/2 tastiera+mouse (USB xHCI e pianificato in Obiettivo 7).
- Avvio diretto in GUI senza splash screen animato (splash previsto come
  estensione futura davanti al compositor).
