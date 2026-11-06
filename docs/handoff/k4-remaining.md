# K4 — stato e lavoro rimanente

4 novembre 2026. Riepilogo di decisioni, lavoro fatto e lavoro da fare,
raccolto durante le review e lo sviluppo di K4. Lo stato verificato resta
in [status.md](../status.md); qui c'è cosa manca e perché.

Aggiornamento del 6 novembre 2026: C4 è chiusa per il backend diagnostico
con quattro AP di calcolo e BSP supervisore; le voci precedenti su C4
qui sotto sono storiche. Il prossimo lavoro è C5, poi C6, K5 e K6.
L'utente ha scelto di svolgere il consolidamento iniziale della sicurezza
dopo i punti K e prima della GUI: vedere
[ordine concordato](security-plan.md#ordine-concordato--6-novembre-2026).

## Regole cambiate in questa fase

In direttive-locali e direttive-locali (file locali, esclusi da git), allineate in
SKILL.md e REVIEW_INSTRUCTION.md:

- **Righe per file sorgente:** fino a 400–500 perfetto, 500–800 buono,
  800–1000 accettabile senza appesantire, oltre 1000 split obbligatorio.
- **File per cartella:** 6–8, massimo 8.
- **Boot QEMU adattivi:** 3 per task piccole, 5 per medie, 10 per SMP,
  interrupt, paging, DMA, privilegi, race e chiusura di milestone.
- **Documentazione:** semplice, essenziale, comprensibile; un design e una
  validazione per blocco, aggiornati invece di moltiplicati.
- **Git:** commit a fine sotto-task, commit e push a fine task completa.
- **Codice nuovo:** niente commenti, solo separatori `// ====== nome ======`
  e documentazione nei file `documentation/`. Unica eccezione: le
  citazioni hardware obbligatorie (Constraint 4).

## Stato di K4

| Parte | Stato | Commit |
| --- | --- | --- |
| K4-A, IPI di servizio | Chiusa | 3c2780d, 16d16c1 |
| Preparazione K4 (geometria per CPU, gruppo da 2 a 64 CPU, helper ICR) | Fatta | 48b7107, 7cec55c, 26044e4 |
| K4-B, shootdown TLB | Chiusa, campagna finale 40/40 | fa35474, b1b33af, 1c19bda |
| K4-C, design preemption SMP | Approvato | f86a2be |
| K4-C1, core Rust SMP | Chiusa | e9ac1d3 |
| K4-C2, timer AP | Verificato nel diagnostico periodico | 4160fe1 |
| K4-C3, preemption BSP | Verificato uno switch one-shot | 4224ce6, 8e818b1 |
| K4-C3, preemption periodica BSP | Verificata nel diagnostico | vedi validazione K4-C |
| K4-C4…C6 | Da fare | — |

Nessun push: la regola git lo prevede alla chiusura di tutta K4.
Il primo commit di K4-B (fa35474) contiene per errore anche l'aggiunta di
`release-missing-ack` ai profili di `build.ps1`; si può spostare con un
rebase prima del push.

## K4-C: decisioni prese

Da [k4c-design.md](../../kernel/scheduler/documentation/k4c-design.md):

- coda globale con un lock, preemption dal timer LAPIC di ogni CPU (D3-A);
- switch sempre via dispatcher della CPU, all'uscita dell'IRQ in Assembly;
- lo stato Rust è protetto da un guard RAII sul `KERNEL_LOCK` di `kernel/sync`;
- tutti i task possono girare su qualsiasi CPU, dopo che i driver sono
  diventati sicuri su più CPU;
- un task si interrompe solo se il frame interrotto è un task con IF=1 e
  senza lock tenuti;
- timeslice iniziale di 10 ms, da misurare;
- Rust proprietario di stato e decisioni, C e Assembly solo per la CPU.

## K4-C1: core Rust SMP

Fatto: `current` e timeslice per CPU, affinità, `tick_on` e `preempt_on`,
guard RAII sul lock (`ffi/core_lock.rs`, cfg `scheduler_core_lock`),
backend C del lock (`integration/lock.c`, rank 900), API C `_on` per CPU,
stress multi-thread sul vero oggetto Rust. Dettagli in
[k4c-validation.md](../../kernel/scheduler/documentation/k4c-validation.md).

C1 è chiusa: suite scheduler, stress 20.000 task su 4 thread 5/5,
regressioni host e 10 boot QEMU con il lock vero. Lo scheduler del kernel
usa ancora solo la CPU 0 fino a C4.

## K4-C: sotto-task rimanenti

1. **C4, dispatcher sugli AP.** Gli AP passano dall'idle di servizio al
   dispatcher, continuando a servire PING e flush. Prima solo task di
   lavoro senza driver. `kernel_scheduler_context_valid` oggi accetta solo
   il BSP sullo stack principale: va esteso per CPU.
2. **C5, driver sicuri su più CPU.** Stato al 6 novembre 2026: coda console,
   produttori AP, IPI 0x43, binding comune BSP/AP e worker BSP in WAIT/HLT
   fatti. Restano integrazione ordinaria e protezione dei driver.
   Audit dei `ks_boot_cpu` raggiungibili
   dai task, poi lock per oggetto (o owner unico) per console, PS/2,
   VirtIO, shell e watchdog. Gli IRQ dei dispositivi restano al BSP.
3. **C6, chiusura di K4.** Stress su 4 CPU, audit G5, accounting,
   regressioni, console VirtIO/VT-d. Poi aggiornamento di status.md e push.

## Punti aperti dalle review

Da fare quando capita, in ordine di utilità:

- **Helper delle attese con deadline:** il ciclo "poll sul PM timer fino al
  timeout" è ripetuto in transport, wait, panic e test. Conviene farlo con
  C2/C3, che aggiungono altre attese.
- **Geometria duplicata fuori da `geometry.h`:** la copia Rust
  (`resources/rust/geometry/pages.rs`) e i runner Python (`17 * 4096`, …)
  hanno ancora i valori a mano.
- **Runner `ap-runtime`:** accetta solo 1 o 4 CPU.
- **Codici di abort con nome** al posto dei magic number a 64 bit.
- **Leggibilità** dei file più densi (`runtime.c`, `dispatch.c`, `percpu.c`).
- **Validazione ripetuta** fra funzioni interne fidate: tenerla ai confini.
- **Build:** primo blocco tra K4 e K5. Spostare in un tool Rust la
  risoluzione dei profili e confrontare, senza compilare, flag, sorgenti
  e ordine degli argomenti con il percorso attuale. Partire dai profili
  ordinario, BSP periodico e AP timer, più le combinazioni rifiutate.
  Dopo la parità, usare il piano Rust nel comando di build e rimuovere
  la logica PowerShell corrispondente. I runner Python si migrano per
  responsabilità, partendo dagli helper condivisi e conservando le prove.
- **Documentazione storica** da accorpare (circa 30 documenti di K3,
  31 file in `console/video/documentation`).
- **Rust:** un unico crate `no_std` del kernel al confine K4/K5, come fonte
  di costanti e tipi condivisi; poi Rust per syscall, user copy ed ELF.

## Dopo K4

- **K5:** SYSCALL/SYSRET, SMEP/SMAP, user copy con recupero dai fault.
  Gate di sicurezza aperti: `swapgs` e controllo del CPL nell'entry IRQ,
  decisione su KPTI, verifica CPUID di SMEP/SMAP, stack cookie e KASLR.
- **K6:** primo processo utente da ELF e fault utente non terminali.
- **Dopo K6:** filesystem, IPC con memoria condivisa, compositor e GUI.
- **Sicurezza, collocazione proposta:** primitive condivise in
  `lib/security/`, politiche specializzate nella sottocartella `security/`
  del modulo che proteggono, visione d'insieme in `docs/security/`.
  Da confermare e aggiungere alle regole di collocazione.
- **Limiti noti:** solo QEMU TCG con xAPIC; hardware fisico, x2APIC
  e PCID mai provati.
