# Stato di VoidOS — 22 novembre 2026

## Immagine ordinaria (`ordinary`, Meson)

- Il loader UEFI carica il kernel ELF. Il kernel configura paging con NX e
  WP, GDT/TSS/IST, allocatore fisico, heap e interrupt. In QEMU è stato
  inizializzato anche il framebuffer GOP.
- In QEMU con quattro vCPU, il core di avvio (BSP) avvia tre core secondari
  (AP). Ogni AP entra in un dispatcher permanente e risponde al PING
  inter-CPU. Il dispatcher persiste anche senza PS/2 e dopo `exit`.
  BSP e AP usano IDT e ingresso IRQ comuni, con sottoscrizioni per CPU.
  Lo scheduler gestisce la sessione PS/2 sul BSP con un timer di preemption
  da 10 ms (vettore D1h). I task della sessione partono con gli interrupt
  attivi e il timer li sospende anche fuori dalla finestra di attesa.
- Con SMEP e SMAP disponibili, il runtime Ring 3 è permanente sull'IDT comune
  con stato di transizione per-CPU (`gs:`), `SYSCALL` attivo stabilmente,
  syscall interrompibili (`IF=1` fuori dalle sezioni critiche minime) e
  tabelle dinamiche di processi, handle e canali IPC su `kernel_heap()` (fino
  a 256). Al boot `init.elf` gira e viene raccolto tramite `x86_process_manager`.
  UMIP viene abilitato se la CPU lo supporta. Lo stack delle syscall ha due
  guard page coperte dall'inventario paging.
- Con PS/2, `ordinary` apre la shell con tastiera e mouse. `bridge status`
  interroga il servizio residente come normale processo gestito senza pausare
  lo scheduler; `proc` avvia 5 processi isolati contemporanei (6 con `bridge`:
  2 coppie server/client utente↔utente via IPC + 1 fault CPL3 `#UD` isolato e
  raccolto senza perdite); `ap <n> [cpu|auto] [tasks]` esegue job fidati sugli
  AP e sul BSP. `exit` chiude la sessione e rilascia processi, task, stack,
  IRQ e binding PS/2. Il dispatcher BSP continua con timer LAPIC e attese
  HPET, anche senza controller PS/2. [Build e boot](../README.md).

## Profili separati

| Profilo o caso | Capacità provata |
| --- | --- |
| `v2c` | Console VirtIO con un dominio VT-d per la GPU, in un profilo separato. |
| `meson-bsp-periodic`, `ap-task-four` | Preemption da timer sul BSP nel primo caso. Nel secondo, otto task kernel fidati distribuiti su quattro AP, con affinità fissa. |
| `k6-process`, `k6-process-smp` | Casi diagnostici di esecuzione in ring 3, fault utente e IPC tra due processi, con cleanup delle risorse. |

Le configurazioni sono nel [registro delle campagne](../scripts/test/campaigns.json).
I profili diagnostici compilati solo da `build.ps1` saranno ritirati man mano
che le loro prove trovano posto nei test host o nell'immagine unica.

## Limiti e prossimo passo

- La 3e chiude le prove della preemption nella sessione PS/2 sul BSP.
  B2 chiude il [dispatcher permanente sul BSP](design/scheduler-produzione.md#prossimo-passo-di-s1)
  in `ordinary`, con cleanup verificato della sessione e continuità senza PS/2.
  Runtime AP permanente e preemption AP sono fatti. Il timer one-shot
  drena il vecchio tick prima del riarmo. I job sono a IF=1.
  Il budget lock è un secondo senza progresso, con cap di un
  milione di poll. La task 5 (selezione senza affinità fissa e migrazione
  tra le CPU ammesse) è fatta; il rendering della console spezza la sezione
  a interrupt spenti per singola cella, azzerando gli overflow della FIFO
  PS/2 sotto carico. Il limite fisso a otto task è stato rimosso: il core
  Rust e il backend x86 supportano 64 task (fino a 256 nel core Rust) con
  tabelle allocate su `kernel_heap()` e quattro PT dedicate per gli stack
  con guard page nello slot heap (`ap <n> [cpu|auto] [tasks]`). Il registro
  IRQ copre 222 vettori sottoscrivibili per CPU.
  I record IRQ delle 64 CPU del bootstrap riservano 568 KiB nell'immagine.
  L'heap è limitato a 16 MiB; `KERNEL_PHYSICAL_LEASES = 64` limita i processi
  simultanei (~40 al netto del boot) e i processi utente girano sul BSP
  mentre gli AP eseguono task kernel.
- Una porzione di audit a IRQ spenti ha raggiunto 18,7 ms nella 3d-4
  e circa 13,5 ms nel soak della 3e. Il quanto nominale è 10 ms.
  [Validazione](validation/consolidamento.md#cpu-e-scheduler).
- Mancano driver a blocchi, VFS, filesystem del kernel ed esecuzione di ELF
  da file. La lettura dell'ELF del kernel avviene nel loader tramite UEFI.
- Mancano sorgente di entropia e stack canary del kernel. W^X per sezione
  dell'intera immagine e overflow reale dello stack syscall sono non
  verificati. Il cancello «base solida» resta aperto. Hardware fisico non
  verificato per il percorso descritto qui.
- Il watchdog HPET rileva e recupera il confronto saltato dall'HPET di
  QEMU 11.1 (circa una volta su un milione di armamenti); recupero osservato
  dal vivo in una sessione da 30 minuti.
- Dubbio aperto, accettato dall'utente il 15 novembre 2026: in 1 sessione da
  30 minuti su 21 (tre QEMU in parallelo), dopo la chiusura della shell, un
  AP non ha confermato lo shootdown del TLB entro 1 s (`K4TLBACK`). Causa
  ignota; ipotesi non verificata: carico dell'host. Non si è ripresentato in
  9 sessioni con diagnostica dedicata.
- Il backend processi è stato diviso. La CI ha un workflow per suite host
  e boot ordinario. Le suite host principali (`clock`, `sync`, `scheduler`,
  `line_editor`, `elf_parser`, `process_policy`, `user_protection`,
  `watchdog_integration`, `log_boundary`, 17 test C+ASan e Rust) girano ora
  in un unico [test/meson.build](../test/meson.build) senza dipendere da
  PowerShell 7; restano negli script PowerShell le suite legacy dei driver
  diagnostici.

## Evidenze

Obiettivo 2 (processi multipli isolati, syscall `IF=1`, IPC multi-canale e
fault CPL3 sull'IDT comune), Meson `ordinary`, kernel SHA-256
`5A921AFFAF0F70B11CE8E6E5C852017AA23D4C2A0D87C554C5D875A9B7A73BB2`, loader
SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
QEMU/OVMF TCG/q35: 10/10 boot `--ordinary-shell --smp 4` (`FreePages: 15990`
stabili, `Overruns: 0`), 3/3 boot `--ordinary-shell --smp 8` (`FreePages:
15778` stabili, `Overruns: 0`), 1/1 boot `--ordinary-no-ps2 --smp 4`, ciascuno
con `proc` (6 processi Ring 3 simultanei, 2 coppie IPC utente↔utente, 1 `#UD`
CPL3 verificato nel trace QEMU, 5/5 raccolti con zero leak), 3× `bridge status`
senza `x86_preempt_pause()`, e `ap 1000000 auto 16`. Suite host Meson **18/18
PASS** (`process_table_host` con ASan + `process_policy_rust` 13/13),
`qemu_runner_unit` **21/21 PASS**. Soak 60 min precedente (`DC4ECB82…`)
confermato su Obiettivo 1. Hardware fisico non verificato.
[Validazione processi](validation/processi.md).

Risultati precedenti con immagini e limiti propri nelle validazioni
[AP nel boot ordinario](validation/consolidamento.md#validazione-dei-backend-x86),
[scheduler e shell](../kernel/scheduler/documentation/validation.md),
[consolidamento e build Meson](validation/consolidamento.md), [processi](validation/processi.md) e [protezioni](validation/sicurezza.md).
L'elenco storico dei checkpoint è nella versione di questa pagina a `efecb8b`.
