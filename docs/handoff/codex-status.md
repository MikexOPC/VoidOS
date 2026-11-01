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

## 4. K1: tappa 5 committata, tappa 5b verificata il 27 ottobre

**Obiettivo:** immagine del kernel a `0xFFFF_FFFF_8000_0000` (slot 511), finestra di codice di
4 MiB. La tappa 5 è nel commit `b3eb7e2`; le modifiche personali dell’utente
restano nella working copy. Unica eccezione autorizzata durante 5b:
i due spazi finali in `configuration/read.c`; la logica personale resta invariata.

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
- **V2c ×10 della tappa 5:** verificata PASS in
  `kernel/build/qemu-run-46bb4ffb4d07487ca39695399a80d3b0/results.json`.

Tappa 5b autorizzata con la richiesta di proseguire: implementata in
`arch/x86/mm/paging/retirement/`, agganciata all'inizio di
`kernel_startup_platform`, dopo stack principale, descrittori e ritiro
vecchio stack. Profilo BSP IF=0, PGE/PCIDE/LA57=0; lettura effettiva dei
registri e verifica dei proprietari GDT/TSS/IDT prima della revoca.
PML4[0] viene azzerata, lo stesso CR3 ricaricato e la generazione avanzata.
Pagine e tabelle del loader restano riservate; nessun recupero RAM.

Prove dedicate: host ASan con 1.000 cicli di successo/rientro e percorsi
di rifiuto; dieci boot con walk indipendente PML4 e controllo GDT/IDT/TR;
un negativo con lettura della capsula prima/dopo revoca e #PF atteso.
Regressioni finali completate: 37 suite host coperte/passate con retry
mirato; 23/23 scenari kernel; 12/12 campagne brevi; V2c 10/10 con
traduzione attiva, zero fallback e budget ripristinato in ogni boot
(`qemu-run-0e1e80c861594423ab6e9e7e662e7e17`). K1 è chiuso per QEMU.
La formattazione finale segue `test.txt`; compilazione e confronto ELF
bootstrap/VirtIO dimostrano codice/layout invariati, salvo timestamp di
debug. [Report completo](../../arch/x86/mm/paging/retirement/documentation/validation.md).
K2-A è stato poi verificato nel profilo isolato: vedere la sezione K2 sotto.
Non è attestata compatibilità con hardware fisico.

## 5. Layout virtuale attuale (K1)

| Slot PML4 | Regione |
|---|---|
| 0 | identità del loader durante il passaggio, revocata dalla tappa 5b prima dei device |
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

- `virtio-discovery`: risolto il 27 ottobre, togliendo esclusivamente i due
  spazi finali autorizzati. Le altre modifiche personali non vanno normalizzate.
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

## K2 — snapshot del 27 ottobre, aggiornamenti successivi sotto

Le primitive K2-A sono pronte nel solo profilo `-TestKernelSync`; non sono
ancora adottate da allocatore e heap. C gestisce i guard e l'ordine dei
lock; Rust le atomiche64; ASM solo IF/PAUSE. Scelta utente: attesa
sorvegliata, fail-stop dopo ticket prenotato, senza cancellazione della coda.
Passano host ASan/Clippy/cppcheck, contesa su 2/4/8 thread (140.000 ticket),
sette negativi host e 22 boot finali (10 probe, 10 ordinari, 2 fail-stop).
[Report](../../kernel/sync/documentation/validation.md).
Il contratto K2-B è in `kernel/mm/physical/documentation/k2-design.md`:
scelta approvata: accessor architetturale, oggi BSP verificato, GS/per-CPU
in K3. Il provider BSP e il binding del clock solo a contesto quiescente
sono implementati e provati nel profilo isolato; il bootstrap ordinario,
l'allocatore, i lettori e l'heap non li adottano ancora. Non dichiarare K2
completato o SMP guest verificato. Il report descrive anche il falso
positivo del conteggio globale degli handle host e il controllo delle
chiusure effettivamente possedute.

Blocco attuale: panic LAPIC_TIMER_INVALID al decimo boot di una campagna
sync (probe lock concluso, watchdog EVENT e cleanup riuscito). Diagnostica
goal/step/valore aggiunta in driver.c, senza cambiare policy/soglie. I
successivi 10+20 boot passano senza riprodurlo: NON è un fix. Perimetro
timer autorizzato dall'utente; continuare l'isolamento prima dell'allocatore.
Fix distinto e verificato: init/bind del clock rifiutano Ticks >= Mask/2,
coerentemente con kernel_deadline_start. Vedere report sync e timer.

Ripresa timer: scelta approvata separazione arm→IRQ e tempo watchdog.
ArmedPm/WaitPm[3] in coda owner (160 byte), DeadlinePm scritto dal driver
dopo stop/EOI riusciti; bridge non lo sovrascrive, idle scrive WaitPm.
Soglie identiche in Rust. Mock ritardo post-consegna, IRQ tardivo,
wrap/zero e stop fallito passano. Dieci boot probe, dieci ordinari finali,
ciclo continuo e negativo LAPIC perso passano. k2-failure.md documenta
il fix della misura dimostrato e la causa originale non catturata.
Allocatore e heap non modificati in questo blocco.

28 ottobre, K2-B preparazione: core estratto in physical/allocation/state.c,
facade/API/header/metadati invariati. Core chiama kphys_validate anziché
rientrare nella facade. Dodici adattatori compilano il nuovo file;
intercettazione pin dei test VT-d preservata. Nuova suite host physical
con ASan/accounting, Rust debug/release e cppcheck.
Il runtime sync ora è sempre collegato; bootstrap inizializza il BSP
prima della memoria, probe usa quello stato. Host bootstrap 369 controlli
e 18 failure iniettate; 13 suite host, 10 probe + 10 ordinari finali e
2 negativi QEMU passano. Prove in k2-core-validation.md. Non sono ancora
inseriti lock del pool: prossimo blocco facade e lettori, poi contesa;
non dichiarare questo snapshot una prova di allocatore/heap SMP-safe o K2 completato.

28 ottobre, implementazione pool: facade protetta da Gate rango 300, query
piccole coerenti per budget/range/lease, transazione release_after_unmap
che conserva pin su saturazione; lettori nei chiamanti migrati. ASan e
14.000 cicli su 2/4/8 thread host passati; 39 suite host verificate in
aggregato con gli adattatori mock corretti e ripetuti. Ultima immagine:
10/10 sessioni GOP consecutive e 1/1 VirtIO/IOMMU, pixel/input/cleanup
verificati. Il blocco successivo heap è descritto sotto. Heap e holdings ancora BSP, nessun AP avviato. Dettagli e
prove finali in `kernel/mm/physical/documentation/k2-pool-validation.md`.


28 ottobre, K2-C heap: scelta A approvata, metadati sotto Gate rango 200;
fill fuori dal lock su memoria riservata, Busy/Pending fino al commit.
Overflow bytes+8 corretto e limiti run/slab verificati. PASS: 200.000
operazioni casuali ASan, 14.000 cicli su 2/4/8 thread host, 40 trasferimenti
sospesi, dieci boot diagnostici e una sessione console GOP. Report:
`kernel/mm/heap/documentation/k2-validation.md`. Prima di dichiarare K2
chiuso resta il smoke set completo; nessun avvio AP e nessun lifecycle
concorrente dei mapper attestato.

29 ottobre, gate finale K2 chiuso nel profilo BSP concordato: smoke set
17/17, compresi 23 scenari kernel-boot, fault intenzionali, VirtIO/IOMMU,
fallback e timeout/recovery. Nessun fix eseguibile richiesto nella campagna.
Ricompilazione heap con hash diverso dal precedente: dieci nuovi boot
consecutivi dell’ELF corrente passano 10/10, senza riusare i vecchi PASS.
Report canonico: `kernel/sync/documentation/k2-integration.md`.
Sorgenti K2 invariati dai manifest dei blocchi precedenti; nessuna attestazione
SMP guest o hardware fisico. Prossimo: contratto K3, non AP già implementati.

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

## K3-A — 29 ottobre: BSP tramite GS

Implementato il passaggio approvato dopo GDT e ritiro stack iniziale,
con provider early ritirato e azzerato, token/clock preservati e nessun
fallback dopo installazione GS non verificata. Cinquanta esecuzioni host,
dieci boot diagnostici, dieci ordinari, due negativi e una regressione
interattiva VirtIO/VT-d passano. K3 resta
aperto per clock/dati per CPU e avvio AP; nessun AP parte in questo blocco.
[Report e limiti](../../arch/x86/kernel/cpu/percpu/documentation/k3-validation.md).
Le regole correnti di direttive-locali impongono massimo 400 righe per sorgente;
la vecchia indicazione fino a 500 in questo handoff è storica e superata.

## K3-B — 29 ottobre: clock PM dei lock BSP

Scelta utente ACPI PM implementata: copia posseduta di porta/capacità,
callback senza lock/allocazioni/GS, prova del progresso a IF=0, bind unico
quiescente e rollback. Wait ha deadline e massimo finito di controlli.
Gli adattatori sono limitati a bootstrap/build e test; lo scheduler,
il watchdog e gli allocatori non cambiano. Il test sync ripristina Wait
quando conclude le prove temporanee.

Gate passati: nuova suite host 50/50 e quattro suite di regressione;
dieci boot diagnostici, dieci ordinari, due negativi attesi e una console
VirtIO/VT-d completa. [Evidenze e limiti K3-B](../../arch/x86/kernel/cpu/percpu/documentation/k3b-validation.md).
AP fermi; prossimo blocco K3: identità, contesti/risorse e ingresso AP,
con contratto dedicato prima dell’avvio. Nessun commit/push in questo blocco.

## K3-C — 29 ottobre: identità CPU di boot

Scelta utente: registro statico, hotplug dopo. Implementato in
arch/x86/kernel/cpu/percpu/identity/ con ownership C e validazione Rust.
Indice logico, APIC ID e UID distinti; BSP verificato a indice zero,
CPU offline separate, nessun AP avviato. Slot GS e clock degli AP non
vengono predisposti solo perché esiste un record di identità.

Passano due test Rust in debug/release, 20 processi C ASan e regressioni
percpu/clock/bootstrap; dieci boot diagnostici e dieci ordinari con
confronto indipendente MADT/snapshot nel monitor; due negativi lock e
una sessione console VirtIO/VT-d completa. Il getter rimane BSP-only:
Ready non è una pubblicazione atomica per AP.
[Report e limiti](../../arch/x86/kernel/cpu/percpu/documentation/k3c-validation.md).

Flags=0 sono gestiti dal piano puro ma rifiutati dal parser MADT preesistente,
che non è stato modificato. Nessun backend x2APIC o hotplug introdotto.
Il seguito richiede contratto per stack con guardie, GDT/TSS/IST privati,
risorse/clock AP e ingresso/avvio. Nessun commit o push in questo blocco.

## K3-D — 29 ottobre: arena AP con rollback

Scelta arena completa implementata in cpu/percpu/resources/. Due lease
pinned per dati/tabelle; stack 64 KiB + tre IST 16 KiB e pagina GDT/TSS per
AP, guardie e RW/NX supervisor. Slot PML4 487, tag tabelle 60–69, snapshot
interno paging di 4.000 byte. Aggiornati inventario, validazione video e
GPU: i nuovi tag non sono heap. Nessun INIT/SIPI/LTR AP.

Suite nuova C ASan/Rust e dieci suite host correlate passate; dieci boot
diagnostici + dieci ordinari dell’ELF finale, due fault intenzionali
verificati e una console VirtIO/VT-d completa con quattro vCPU configurate,
ma solo BSP attivo. Nel test host VirtIO i file oggetto hanno ora nomi
univoci per percorso, evitando collisioni fra registry.c; link con /WX.
[Prove, regressioni corrette e limiti](../../arch/x86/kernel/cpu/percpu/documentation/k3d-validation.md).

Il rilascio transazionale conserva le lease rimaste pinned su rifiuto;
fallimento del rollback è terminale. Risorse normali mantenute PREPARED.
Prima di avviare AP introdurre commit/non-rilascio, contesti/clock/GS AP,
compatibilità dei percorsi fault/NMI, riserva bassa e CR3/trampolino d’ingresso.
La riserva SIPI deve precedere le allocazioni generiche. Nessun commit/push.

## K3-E — 29 ottobre: contesti AP e commit del lifetime

Scelta utente: mantenere rollback fino al tentativo di avvio. Implementati
contesti AP PREPARED con token 1 e frame vuoti, copie PM private, cleanup
prima del commit e sigillo non reversibile dopo il commit. API nativa
`x86_ap_resources_commit()` per il futuro ingresso; l'accessor operativo
resta BSP-only. Risorse ordinarie non committed; test isolato
`-TestApCommit`, campagna registrata `percpu-ap-commit`.

Sei suite host pertinenti passate (clock 70 esecuzioni C ASan, risorse
C/Rust con 101 failure point e prove commit/refusal, provider, identita,
kernel e bootstrap). Immagini finali: dieci boot diagnostici a quattro
vCPU, dieci ordinari alternando una/quattro vCPU, una console VirtIO/VT-d
con pixel/input/cleanup a quattro vCPU. Un solo core OS operativo.
[Evidenze e limiti K3-E](../../arch/x86/kernel/cpu/percpu/documentation/k3e-validation.md).

Il gate commit/contesti/clock predisposti e ora chiuso. Restano fault/NMI
per CPU, riserva bassa e trampolino/CR3, ingresso e avvio AP. Nessuna
promessa di SMP dal numero di vCPU. Nessuna modifica a reuse/, commit o push.

## K3-F — 30 ottobre: backend panic verificato sul BSP

Scelte utente: panic SMP completo e NMI diretti con attesa limitata.
Implementati record privati, elezione/ammissione/ACK atomici Rust,
cattura Assembly senza GS, trasporto LAPIC permanente e output COM1
polled separato dal logger. Preparazione transazionale con rollback;
commit prima dell'attivazione, nessun rilascio dopo. Attivazione solo nei
diagnostici isolati; percorso ordinario ancora locale.

Prove finali: 21/21 scenari diagnostici, inclusi dieci prepare consecutivi,
10/10 ordinari, 1/1 console VirtIO/VT-d; suite panic ASan/Rust e regressioni
kernel/bootstrap/risorse passate. Audit di undici oggetti senza FP/SIMD,
due prove CR0.TS reali. Manifesto locale kernel/build/k3f-final-validation.json.
[Report completo](../../arch/x86/kernel/cpu/percpu/documentation/k3f-validation.md).

Non chiudere il gate SMP: solo BSP eseguito. Prossimo lavoro necessario:
riserva bassa/trampolino/ingresso AP e poi ammissione e NMI remoti su core
OS vivi. L'accessor operativo resta BSP-only. Non attivare AP senza
rispettare lifetime, descrittori, clock e protocollo di ammissione.
Nessuna modifica a reuse/, commit o push. Il diff generale contiene
trailing whitespace estranei al blocco in main.c e startup.h, non corretti.

## K3-G — 30 ottobre: pagina bassa riservata nel kernel

Scelta utente A implementata: dopo initialize/validate fisico e prima di
window/regioni, una pagina sotto 1 MiB diventa la prima lease pinned.
Nuova API ammette soltanto allocatore fresco; nessun indirizzo fisso,
nessun mapping o codice AP. C possiede la risorsa, Rust valida geometria.
Indisponibilità: WARN, BSP continua senza startup lease. Il diagnostico
-TestApLowAbsent inietta il caso; -TestBootstrapMemory resta separato.

Dieci boot dedicati e dieci ordinari finali passati; negativo 1/1 e console
VirtIO/VT-d 1/1 con pixel/input/cleanup. Host low/bootstrap/kernel passati;
allocatore ASan e 2/4/8 thread passati in isolamento. Un tentativo parallelo
a QEMU aveva superato la deadline host: evidenza fallita conservata e limiti
invariati nella ripetizione. Eseguire host e QEMU pesanti in sequenza.
[Report K3-G](../../arch/x86/kernel/cpu/percpu/documentation/k3g-validation.md).
Manifesto locale kernel/build/k3g-final-validation.json.

Prima dell'avvio servono commit irreversibile anche per la pagina bassa,
root/tabelle di transizione, ingresso 16/32/64 bit e gestione fault/NMI
precoce, poi GDT/TSS/GS privati e ammissione. Solo dopo prove SMP K3-F.
Riserva nativa privata; non esporre release durante o dopo un tentativo AP.
Nessun AP OS attivo, hardware fisico, commit/push o modifica a reuse/.

Campagna percpu-low verificata anche dal comando registrato, smoke 1/1:
qemu-run-decb077c6cfd4fd68687296f0ab3b300. Usare PowerShell 7 e -Parallel 1:
il percorso seriale esegue build e worker nello stesso host, evitando i
wrapper annidati rimasti aperti nei tentativi precedenti. La conversione
dei parametri del registro non usa Invoke-Expression. Il ramo parallelo
non è ricertificato qui. Tentativi interrotti conservati nel report.
Test Rust centralizzati; oggetto di produzione identico prima/dopo lo
spostamento, hash nel manifesto. Ultima suite low:
test-run-a225c3ca589945c7af81d1ce80ed1ee6.

## K3-H — 30 ottobre: preparazione inattiva verificata, v5

Root condivisa implementata in cpu/percpu/transition: pagina dati sotto
1 MiB distinta dal codice, quattro frame sotto 4 GiB, codice RO/X e dati
RW/NX, riferimenti kernel condivisi, ricorsivo assente. Snapshot/scratch
statici, rollback completo prima dell'attivazione, lease pinned e hold sul
codice. CR3 del BSP invariato. Prima del futuro avvio vanno rivalidati i
riferimenti e introdotti commit, trampolino e ingresso AP.

La console v3 isolata superava 600 s, evidenza conservata in
pointer-interactive-check-b4068248bbca431b82cce4f109a76183. Estensione
paging/memoria autorizzata: inventario ordinato lineare con fallback,
ricerca binaria degli alias e confronti fra estremi già validati sotto
lock. Nessuna cache di autorizzazioni e nessuna soglia allentata.

Finale v5: 10/10 preparazioni, rollback 1/1, 10/10 boot ordinari e 10/10
console VirtIO/VT-d consecutive, inclusi pixel, movimento, input e cleanup.
Host transizione, paging, bootstrap/kernel e allocatore ASan/2/4/8 thread
passati. 19.478 confronti differenziali ASan; source max 379 righe e cinque
file diretti nel perimetro. Cppcheck finale: 29 information e due style
preesistenti, zero diagnosi bloccanti. Copertura linee/branch N/D.

Manifesto: kernel/build/k3h-source-manifest.json. Immagini:
kernel/build/k3h-final-{prepare,rollback,normal,console,legacy}-v5/.
Console: pointer-interactive-check-0296a0ad972744f88b53e4aefe355a5f/results.json.
Le osservazioni host VT-d→puntatore, circa 201–511 s, risentono anche del
polling diagnostico alleggerito dopo il quarto boot: non sono un benchmark
controllato. Lo startup rimane lento e richiede profiling futuro.

[Report completo K3-H](../../arch/x86/kernel/cpu/percpu/documentation/k3h-validation.md).
Preparazione e rollback K3-H verificati; nessun AP avviato. K3-F SMP resta
aperto: quattro vCPU configurate non dimostrano quattro core OS attivi.
Hardware fisico e Valgrind non eseguiti. Nessuna modifica a reuse/, commit
o push.
