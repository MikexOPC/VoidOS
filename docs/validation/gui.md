# Validazione — Obiettivo 6: GUI di VoidOS

## Stato
**Fatta** (sull'immagine di produzione `ordinary` in QEMU; hardware fisico non ancora provato).

- `kernel.elf` SHA-256: `9B2F12D99C8CB32A5B4FDF1EF5551426A8E412E021C040E5315E6B474F7CC8A9`
- Binari userspace GUI sul disco VirtIO-blk FAT32 (4 MiB, 8192 settori):
  - `/bin/ui.elf` (`user-ui.elf`): 37.264 byte
  - `/bin/terminal.elf` (`user-terminal.elf`): 29.080 byte
  - `/bin/sysmon.elf` (`user-sysmon.elf`): 24.984 byte
  - `/ETC/UI/THEME.VSS`: tema mini-CSS dark "Void"

---

## 1. Test host (ASan / Rust)

| Suite | Risultato | Cosa verifica |
|---|---|---|
| `gui_host` (C + ASan) | **PASS** (`KERNEL_GUI_HOST_OK`) | Ownership esclusiva display, bounds/overflow rect `uint32_t`, input ring FIFO + overflow drop, crescita dinamica SHM (`>16` oggetti, `>32` mapping), refcounting multi-PID, rilascio automatico al termine processo, non-escalation `BUDGET_FLAG_DISPLAY`. |
| `libui_rust` | **4/4 PASS** | Easing fixed-point 16.16 e `Tween`/`RectAnim`, parser mini-CSS `.vss`, `Canvas` fill/clip/blend + `DirtyList`, roundtrip + magic check `GuiMessage` (`96` byte). |
| `libvoid_rust` | **3/3 PASS** | Dimensioni ABI struct (`DisplayInfo`=48, `DisplayAcquireOut`=32, `DisplayRect`=16, `InputEvent`=24), token splitting, formattazione decimale. |
| `process_table_host` (C + ASan) | **PASS** | Tabella processi dinamica, IPC, non-escalation budget, zero leak heap. |
| `security_random_host` (C + ASan) | **PASS** | Entropia, RCT/APT, `HMAC_DRBG`, canary `/GS`. |

---

## 2. Prove sull'immagine di produzione in QEMU

- **Campagna:** `qemu_runner.exe --image kernel/build/meson-kernel/kernel.elf --ordinary-shell --smp 4 --boots 10`
- **Risultato:** **10/10 PASS** (`kernel/build/qemu-runner-18eab0b67e2a3b98-2ab0`).
- **Comportamento reale verificato su ogni boot:**
  1. `SYS_DISPLAY_INFO` restituisce geometria valida del framebuffer attivo.
  2. `SYS_SHM_CREATE` / `SYS_SHM_MAP` / `SYS_SHM_UNMAP` mappano pagine `RW+NX` in Ring 3 (`0x10000000+`), verificano lettura/scrittura volatile e rilasciano senza leak fisici (controllo watchdog HPET).
  3. `/bin/ui.elf` carica `/ETC/UI/THEME.VSS` dal VFS, acquisisce il display con `BUDGET_FLAG_DISPLAY`, mappa il backbuffer SHM (768 pagine per 1024x768x32), compone i 5 strati modulari (`ui/components/{background,window,dock,overlay,cursor}`), esegue il morph animato della Dock (`Floating` -> `Bar`), presenta i dirty rect sul framebuffer e all'uscita rilascia il display ripristinando la console testuale.
  4. `/bin/terminal.elf` e `/bin/sysmon.elf` vengono avviati **concorrentemente** in sandbox (senza `BUDGET_FLAG_DISPLAY`), confermano il rifiuto di `display_acquire`, allocano/mappano ciascuno il proprio buffer SHM, eseguono comandi VFS reali (`print`, `fl /`, `rp /HELLO.TXT`) e animazioni `Tween` rasterizzando pixel reali, e rilasciano tutto a zero leak.

---

## 3. Limiti noti rimasti

- **Sessione live multi-finestra IPC:** il loop interattivo `mode=0` di `/bin/ui.elf`, `/bin/terminal.elf` e `/bin/sysmon.elf` e implementato; nei boot automatici `--ordinary-shell` i binari girano in `mode=1` per non bloccare il runner della shell testuale.
- **Rendering:** 100% CPU software (`SSE2` + dirty rects); nessuna accelerazione GPU 3D.
- **Font:** bitmap ASCII 8x8 scalato 8x16 (niente TrueType).
- **Hardware reale:** non ancora provato su PC fisico (previsto in Obiettivo 7 insieme a USB xHCI e x2APIC).
