# Passaggio di consegne a Codex: stato di MikeSW OS

26 ottobre 2026. Questo documento descrive dove si trova il progetto, cosa è stato fatto nelle
ultime sessioni, cosa è in corso e cosa manca. Prima di qualsiasi lavoro leggere `direttive-locali`
(regole vincolanti) e i design in `docs/design/`.

## 1. Il progetto in breve

- Kernel x86-64 avviato da un loader UEFI proprio (`BOOTX64.EFI`), in C (MSVC), Rust (`no_std`,
  oggetti collegati al C) e MASM. Testato in QEMU/OVMF con TCG.
- Un solo core (il BSP); scheduler cooperativo a 8 task; nessun processo utente.
- Console grafica interattiva con tastiera e mouse PS/2, shell, presentazione tramite VirtIO GPU
  tradotta da VT-d (IOMMU), con ripiego sul framebuffer GOP.
- Struttura delle cartelle: `arch/x86/` (codice x86), `kernel/` (portabile), `lib/` (librerie
  senza dipendenze), `test/` (stessa struttura del sorgente), `scripts/` (build e test),
  `docs/`, `reuse/` (vecchio OS: solo riferimento, non analizzare né modificare).

## 2. Regole da rispettare (dall'utente e da direttive-locali)

- Prima il design, poi il codice: le scelte con alternative vanno presentate all'utente e
  approvate. Un modulo alla volta.
- File sorgente: circa 400 righe (indicativo, fino a ~500 accettabile; la VirtIO discovery ha un
  controllo rigido a 400). Al massimo 5 file per cartella. Nessuno spazio a fine riga. Codice
  spaziato e leggibile, separatori `// ====== nome ======`.
- Accessi hardware con citazione della fonte (Intel SDM, VirtIO, ACPI…).
- Ogni scelta tra alternative resta isolata (in un solo posto), per poterla migrare.
- Prove: test host (MSVC con ASan, Rust, cppcheck), campagne QEMU; per un modulo pronto, dieci
  boot consecutivi del profilo toccato.
- **Commit:** solo su richiesta esplicita, **senza co-autore**. Non committare mai i file
  dell'utente con modifiche locali: `.gitignore`, `kernel/main.c`, `kernel/shell/queue.c`,
  `kernel/shell/shell.h`, `kernel/startup/devices.c`, `kernel/startup/profile.c`,
  `kernel/startup/startup.h`, `kernel/drivers/video/virtio/discovery/configuration/read.c`,
  `lib/memory/address_ranges.h`, `test.txt`. Se serve un proprio hunk in uno di questi file (è
  successo con `devices.c`), lo si mette nell'indice partendo dalla versione di HEAD
  (`git hash-object -w` e `git update-index --cacheinfo`), non con `git add`.
- L'utente scrive in italiano: rispondere in italiano.

## 3. Cronologia recente (commit su `main`)

| Commit | Contenuto |
|---|---|
| `c895d76` | B8: memoria convenzionale UEFI con XP accettata; solo la pagina 0 riservata |
| `412a87c` | G1: librerie `lib/geometry` (rect, damage) e `lib/containers` (ring, bitmap) |
| `74c6b5c` | Present VirtIO divisa in `present/` sotto le 400 righe |
| `978a5b9` | G2: infrastruttura di test comune (`scripts/lib`, registro delle campagne, smoke set) |
| `5def800` | G3: heap del kernel (slab e page run) |
| `e0ce1c1` | G4: clock condiviso (`kernel/time`) |
| `befea77` | G5: verifica del paging fuori dal percorso caldo (chiave di generazione, audit in idle, tamper test) |
| `796fbf2` | G6: driver come oggetti (present, sessione VT-d, console) e interfaccia `KERNEL_DISPLAY` |
| `c0a64fa` | Livelli di test: cppcheck senza blocchi sulla pipe, `qemu.ps1 -Parallel`, deadline V2b a 600 s |
| `c16bcb2` | `scripts/clean.ps1` per `kernel/build` |
| `b19e4d0` | K1 tappa 1: `arch/x86/configs/layout.h` unica fonte degli indirizzi, senza cambi di comportamento |
| `3816775` | K1 tappa 2: device e heap negli slot 481–486 |
| `32ce751` | K1 tappa 3: finestre di boot, stack, arena e LAPIC negli slot 479, 480 e 488 |
| `9449202` | K1 tappa 4: vista ricorsiva nello slot 510 |

Design di riferimento: `docs/design/g6-driver-objects.md`, `docs/design/g7-smp-user.md`
(roadmap SMP e user mode, decisioni D1–D7 approvate), `docs/design/k1-higher-half.md`
(K1, con la tabella di avanzamento al §7), `docs/mm/paging/verification-policy.md` (G5).

## 4. Lavoro in corso: K1 tappa 5 (non committata)

**Obiettivo:** immagine del kernel a `0xFFFF_FFFF_8000_0000` (slot 511), finestra di codice di
4 MiB. Le modifiche sono nella working copy (`git status`), tutte di Claude tranne i file
dell'utente elencati al §2.

Cosa è stato cambiato:
- `layout.h`: `X86_KERNEL_IMAGE_BASE = 0xFFFFFFFF80000000`, `X86_KERNEL_IMAGE_BYTES = 0x400000`.
- **Loader:** validazione dei segmenti ELF in Rust (`segment_bounds.rs`) e in C (`segments.c`,
  `resources.c`), entry nella capsula (`capsule_profile.rs`, rifiuta `u64::MAX` perché le
  sovrapposizioni calcolano `entry + 1`), GDT e IDT di transizione (`arch/x86/lib/descriptors`).
- **Kernel:** entry del contratto (`kernel/boot/contract.c`), registro IRQ, contesti e mapping
  dello scheduler, Rust delle eccezioni e del ritorno da IRQ: tutti accettano la metà alta con la
  stessa regola (`X86_LAYOUT_RANGE` o la sua copia Rust).
- `kernel/scripts/convert-image.ps1`: limite delle sezioni derivato da `$OsImageBase`.
- Misura della metà bassa: `x86_paging_lower_slots` (`arch/x86/mm/paging/access.c`), stampata
  all'avvio dell'heap (`arch/x86/mm/heap/binding.c`) come `KERNEL_LOWER_HALF_SLOTS`,
  `KERNEL_LOWER_HALF_FIRST` e `KERNEL_LOWER_HALF_EMPTY` oppure `KERNEL_LOWER_HALF_USED` (avviso).
- Test host e runner aggiornati con la nuova base (contratto, IDT/GDT, tabella IRQ, terminali delle
  eccezioni, finestra di pagina, probe del loader, runner del timer LAPIC).

Stato delle prove sulla tappa 5:
- Loader e kernel compilano; i test host del loader passano.
- Suite host: tutte passano tranne `virtio-discovery` (vedi §7).
- Smoke set: 11/12, con `kernel-boot` (23 scenari) rieseguita dopo la correzione del runner e
  passata.
- **V2c ×10:** lanciata alla fine della sessione; il risultato va verificato
  (`scripts/test/qemu.ps1 -Campaign v2c`).

Cosa manca per chiudere K1:
1. Risultato di V2c ×10, poi il commit della tappa 5 (solo su richiesta dell'utente).
2. **Tappa 5b, slot 0 (da proporre all'utente, non ancora approvata).** Sotto lo slot 256 resta
   solo lo slot 0, cioè la mappatura identità della transizione del loader: pagina della capsula
   (RX), pacchetto dei descrittori e due stack ausiliari (`arch/x86/boot/uefi/paging/plan.c`).
   Proposta:
   - rimozione subito dopo che il kernel ha installato GDT, TSS, IDT e stack principale propri;
   - prima di rimuovere, verificare che GDTR, IDTR, TR, RSP e RIP non puntino nello slot 0
     (altrimenti panic);
   - azzerare PML4[0] e ricaricare CR3; le tabelle e le pagine del loader restano riservate;
   - rendere obbligatorio `KERNEL_LOWER_HALF_EMPTY` in `kernel-boot` e aggiungere uno scenario
     negativo che tocca il vecchio indirizzo della capsula e deve dare #PF.

## 5. Layout virtuale attuale (K1)

| Slot PML4 | Regione |
|---|---|
| 0 | identità della transizione del loader (da rimuovere, tappa 5b) |
| 1–255 | vuoti, riservati all'utente |
| 479 | mappa della memoria di boot |
| 480 | contratto di boot |
| 481–486 | framebuffer, GPU, superficie, IOMMU, DMA, heap |
| 488 | finestre: stack di boot, finestra di pagina, IST, stack principale, arena, LAPIC, stack dei task |
| 510 | vista ricorsiva |
| 511 | immagine del kernel (`0xFFFF_FFFF_8000_0000`, 4 MiB) |

Regole che chi modifica il layout deve conoscere:
- **Unica fonte:** `arch/x86/configs/layout.h`. Helper: `X86_LAYOUT_SLOT_ADDRESS` (estende il segno
  per gli slot ≥ 256), `X86_LAYOUT_SLOT_OF` (indice con maschera `& 511`),
  `X86_LAYOUT_RECURSIVE_VIEW`, `X86_LAYOUT_RANGE` (intervallo in una sola metà canonica, mai la
  pagina 0), `X86_KERNEL_IMAGE_CONTAINS`.
- **Script:** `scripts/lib/layout.ps1` (`$OsImageBase`, `$OsImageLimit`, `$OsWindows`,
  `Get-LayoutSlotAddress`) e `scripts/lib/layout.py` (`slot`, `slot_address`, `KERNEL_IMAGE_*`)
  leggono `layout.h`. Hanno sostituito il log seriale previsto da L3-A: è una deviazione
  documentata.
- **Copie Rust:** il Rust non può includere `layout.h`; le copie (`KERNEL_IMAGE_BASE`, `WINDOWS`,
  `LAPIC_VIRTUAL`, `VIDEO`, `RECURSIVE`) sono confrontate a ogni build del kernel da
  `Assert-LayoutCopies` (`scripts/lib/layout.ps1`).
- **Suite `layout`** (`scripts/test/layout-check.ps1`): fallisce se compare un indirizzo del
  kernel scritto a mano fuori da `layout.h` (C, Rust, MASM, Python, PowerShell; stringhe
  esadecimali e forme decimali comprese). `test/` è esente tranne i `.asm`: lì i fixture con slot
  scritti a mano vanno cercati a mano quando si sposta qualcosa.

## 6. Come si prova

- `scripts/test/run.ps1`: tutte le suite host (circa 7 minuti); `-Suite nome` per una sola.
- `scripts/test/qemu.ps1 -List | -Campaign a,b | -Smoke | -Boots N | -Parallel N`: campagne QEMU
  da `scripts/test/campaigns.json`. Le campagne `heavy` (VirtIO/IOMMU) girano una alla volta.
  Lo smoke set completo dura circa 25–30 minuti.
- Build: `kernel/scripts/build.ps1` con gli switch del profilo (per esempio V2c:
  `-Ps2Input -Ps2Mouse -ConsoleShell -InteractiveInput -VgpuIommu -OutputDirectory <dir>`);
  loader: `arch/x86/boot/uefi/scripts/build.ps1 -BootKernel` (senza `-BootKernel` lascia in `esp/`
  il loader diagnostico e rompe le campagne successive).
- Python: `~/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe` (non c'è
  un `python` di sistema).
- Pulizia: `scripts/clean.ps1` (simulazione), `-Apply` per cancellare; tiene le ultime 3
  esecuzioni per tipo, quelle citate nei documenti e le evidenze annidate.

## 7. Problemi noti

- `virtio-discovery` fallisce per le modifiche locali dell'utente a
  `kernel/drivers/video/virtio/discovery/configuration/read.c` (non toccare).
- La campagna del mapping VirtIO (`arch/x86/kernel/drivers/video/virtio/scripts/firmware.py`,
  build `-TestVgpuMapping`) aspetta `KERNEL_POINTER_READY` e fallisce allo stesso modo anche prima
  di K1: è un runner da sistemare a parte.
- Varie campagne vecchie (QI, mapping, inizializzazione VirtIO, discovery IOMMU) non sono nel
  registro `campaigns.json` (FW-012): si lanciano a mano.
- `interactive.py`: la deadline V2b è 600 s perché TCG rallenta con il carico della macchina.
- Gli screenshot PPM dei boot passati occupano circa 1 GB per una campagna V2c da 10 boot; non
  sono ancora cancellati automaticamente per i boot PASS (proposta aperta).

## 8. Insidie già incontrate (da non ripetere)

- PowerShell 5.1: `& exe ... 2> file` lascia cppcheck bloccato sulla pipe; usare `Start-Process`
  (`Invoke-CppcheckRaw` e `Invoke-Cppcheck` in `scripts/lib/common.ps1`).
- `... | Select-Object -First N` dopo un comando lungo lo interrompe; filtrare con
  `Select-String` senza `-First`, oppure salvare l'output e poi filtrarlo.
- In PowerShell la virgola lega più del `+`: `@('a', $x + 1)` va scritto `@('a', ($x + 1))`;
  `@(@(...))` con un solo elemento si appiattisce, serve `@(,@(...))`.
- Un'immagine vecchia che smette di passare indica l'ambiente (loader, deadline, macchina lenta),
  non il codice: confrontare con un worktree sul commit precedente prima di correggere.
- Spostando uno slot, cercare anche: indici `[128]`/`[224]` nei fixture, `root + N * 8` e
  `pml4[N]` nei runner, indirizzi in decimale, stringhe esadecimali senza `0x`, letterali MASM
  (`...h`), copie Rust con `_`.

## 9. Roadmap dopo K1

Ordine approvato in `docs/design/g7-smp-user.md`:

| Passo | Contenuto |
|---|---|
| K2 | Lock (ticket spinlock con irqsave) e atomiche; allocatore e heap con lock |
| K3 | Avvio degli AP (INIT-SIPI-SIPI) e dati per CPU (GS base) |
| K4 | TLB shootdown e scheduler con preemption (coda globale) |
| K5 | SYSCALL/SYSRET, SMEP/SMAP, copie da e verso l'utente con recupero dai fault |
| K6 | Primo processo utente (ELF dall'ESP) e fault utente non terminali |

Idee discusse con l'utente, non ancora nei design:
- **D8, proposta:** ABI delle syscall nativa a capability, progettata in modo che più avanti si
  possa aggiungere uno strato di compatibilità con Linux (per far girare Chromium).
- **Rust per l'input non fidato:** parser ACPI/MADT, ELF utente, argomenti delle syscall. Il primo
  candidato è il parser ELF di K6.
- **App interne e browser:** prima NetSurf (frontend framebuffer), poi un motore più completo, e
  Chromium (il browser dell'utente si basa su Chromium) solo tramite la compatibilità Linux.
  Libreria utente con due implementazioni (syscall vere e mock su host) per testare le app su
  Windows.
- **GUI:** dopo K6 servono filesystem, IPC con memoria condivisa, un compositor in user space dietro
  `KERNEL_DISPLAY`, font TrueType e widget.
