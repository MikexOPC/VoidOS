# Design — Obiettivo 4: Esecuzione da file (`exec` / `spawn`, `init`, `libvoid`, shell Ring 3)

## 1. Obiettivo

Il kernel carica ed esegue programmi ELF dal VFS (`/bin/`) tramite `SYS_SPAWN` con budget di risorse per-processo, avvia `/bin/init.elf`, supervisiona `/bin/bridge.elf` come servizio permanente di mediazione di sicurezza e fornisce una shell interattiva in Ring 3 (`/bin/sh.elf`) basata su `libvoid`.

## 2. Architettura e principi

- **Creazione processi (`SYS_SPAWN`, niente `fork()`):** il kernel crea ogni processo direttamente da un file ELF aperto tramite VFS, evitando duplicazione di page table, Copy-on-Write e TLB shootdown non necessari su SMP.
- **Risorse separate e budget per-processo:** descrittori di file VFS, handle IPC e PID restano separati per mantenere un confine semplice verso un futuro strato di compatibilità POSIX/libc, ma ogni processo riceve allo `spawn` un `KERNEL_PROCESS_BUDGET` vincolante:
  - `MaxImagePages`: tetto massimo di pagine immagine ELF (default `128`, pari a 512 KiB, entro la PT utente `[0x400000, 0x5fd000)` da 509 pagine).
  - `MaxOpenFiles`: tetto di file VFS aperti contemporaneamente dal processo (default `32`).
  - `CpuBudgetMs`: budget temporale del turno di esecuzione (default `2000` ms).
  - `Flags`: bitmask di diritti di sistema (es. permesso di spawn).
- **Regola di non-escalation:** un processo figlio può ereditare o restringere il budget del padre, ma non può mai superarlo. Qualsiasi richiesta con valori superiori al padre o oltre i limiti di sistema viene rifiutata dal kernel.
- **Ruolo di `/bin/bridge.elf`:** servizio permanente in Rust Ring 3 avviato e supervisionato da `/bin/init.elf`. Media le chiamate di sistema e di autorizzazione (e in futuro la comunicazione tra GUI e servizi di sistema) tramite protocollo versionato in `bridge/api/`, mantenendo parsing e policy fuori dal kernel.
- **Struttura userspace:**
  - `userspace/libvoid/`: libreria nativa `no_std` (`x86_64-unknown-none`) con wrapper syscall e helper comuni.
  - `userspace/init/`: `/bin/init.elf`.
  - `userspace/shell/`: `/bin/sh.elf`.
  - `userspace/worker/`: `/bin/worker.elf`.
  - `bridge/service/`: `/bin/bridge.elf`.
  - Futuro (dopo il cancello «base solida»): `userspace/gui/ui/` (compositor/desktop) e `userspace/gui/apps/` (applicazioni utente).

## 3. Spazio di indirizzamento utente e caricamento ELF

- Intervallo immagine ELF: `[0x400000, 0x5fd000)` (509 pagine disponibili in una singola PT utente).
- Stack utente: `[0x5fe000, 0x600000)` (2 pagine, RW + NX, con pagina non mappata a `0x5fd000` come guard).
- Limite immagine: `KERNEL_PROCESS_MAX_IMAGE_PAGES = 128` (512 KiB). Il piano ELF (`PROCESS_PLAN`, ~4 KiB) non viene allocato sullo stack del kernel ma su `kernel_heap()` (con fallback statico nei profili diagnostici senza heap).
- Mappatura fisica: `map_image` e `reap` usano `x86_page_window_allocate_lease`, `x86_page_window_write_lease` e `x86_page_window_free_lease`, che operano in finestre scorrevoli da `X86_KERNEL_WINDOW_PAGES` (16 pagine) senza alterare il layout virtuale del kernel.
- Protezioni invariate: W^X stretto (`PF_W | PF_X` rifiutato dal parser Rust), NX su dati/stack, SMEP/SMAP/UMIP, azzeramento completo e TLB shootdown prima del rilascio fisico.

## 4. Layout del disco di sistema (`/`)

Sul volume FAT32 VirtIO-blk (`vblk.img`) montato in `/`:
- `/HELLO.TXT` (fixture di verifica VFS).
- `/bin/init.elf`
- `/bin/bridge.elf`
- `/bin/worker.elf`
- `/bin/sh.elf`

## 5. Syscall introdotte in Obiettivo 4

| Numero | Nome | Argomenti (`rdi`, `rsi`, `rdx`) | Esito (`rax`) |
| --- | --- | --- | --- |
| `11` | `SYS_SPAWN` | `path_ptr`, `path_len`, `spawn_args_ptr` (arg + budget opzionale) | `pid > 0` oppure codice errore negativo |
| `12` | `SYS_WAIT_PID` | `pid`, `status_out_ptr` (opzionale), `flags` | `0` a raccolta avvenuta, errore negativo altrimenti |
| `13` | `SYS_LIST_DIR` | `path_ptr`, `path_len`, `out_buf_ptr` (con capacità fissa nel descrittore) | numero byte/voci scritte o errore negativo |
| `14` | `SYS_SYNC` | `0`, `0`, `0` | `0` se flush ok, errore negativo altrimenti |
| `15` | `SYS_CONSOLE_READ` | `buf_ptr`, `buf_len`, `flags` | byte letti o errore negativo |

## 6. Errori e limiti noti

- Un binario ELF assente, troncato, non-x86_64, con segmenti `W+X` o con `page_count > budget.MaxImagePages` viene rifiutato senza allocare pagine fisiche permanenti.
- Un processo Ring 3 che va in fault (`#UD`, `#PF`, `#GP`) o esaurisce il budget CPU viene terminato dal kernel; `SYS_WAIT_PID` restituisce al padre lo stato di uscita (`ExitKind`, `ExitCode`) e tutte le pagine fisiche, fd VFS e handle IPC sono rilasciati.
- Limiti noti iniziali: spazio immagine limitato a 1 PT utente (128 pagine = 512 KiB), nessuna allocazione heap dinamica utente (`mmap`/`brk`) in questa milestone, console kernel mantenuta come fallback se `/bin/sh.elf` manca o termina.
