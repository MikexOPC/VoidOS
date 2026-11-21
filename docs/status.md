# Stato di VoidOS — 21 novembre 2026

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
- Con SMEP e SMAP disponibili, il BSP esegue un ELF Rust incorporato in
  ring 3 con tabelle di paging dedicate. Il programma stampa via syscall,
  termina e viene rilasciato prima di `KERNEL_READY`. UMIP viene abilitato
  se la CPU lo supporta. Lo stack delle syscall ha due guard page.
- Con PS/2, `ordinary` apre la shell con tastiera e mouse. `bridge status`
  interroga il servizio residente e `ap <n> [cpu]` invia un job fidato
  all'AP scelto, con AP1 predefinito.
  `exit` chiude la sessione e rilascia task, stack, IRQ e binding PS/2.
  Il dispatcher BSP continua con timer LAPIC e attese HPET, anche senza
  controller PS/2. Nell'attesa gli interrupt sono abilitati.
  [Build e boot](../README.md).

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
  milione di poll. Il prossimo passo è la selezione senza affinità fissa,
  task 5. Restano otto
  task. Il registro IRQ copre 222 vettori sottoscrivibili per CPU.
  I record IRQ delle 64 CPU del bootstrap riservano 568 KiB nell'immagine.
  L'heap è limitato a 16 MiB e c'è
  un solo processo attivo alla volta.
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
  e boot ordinario. Resta la migrazione delle suite host in `meson test`.

## Evidenze

Preemption AP, Meson `ordinary`, kernel SHA-256
`8F24D4FF84B55534CC204D54AFE5B2D7D3F29EBCDEA9B5B4F35142C29DFB7BE7`.
QEMU/OVMF TCG/q35, 25/25 boot della matrice e due boot mirati superati,
con shell, PS/2 assente, SMAP assente e una, quattro oppure otto vCPU.
Stress PASS con 15 minuti di shell e 15 dopo `exit`, 116 job su sette AP,
10.105 byte PS/2 prodotti e letti, zero sforamenti e 15.814 pagine stabili.
Otto suite host e runner Meson 2/2, con 20 casi unitari e otto di processo.
Il report della VM parallela fallisce per il vecchio gate del drenaggio.
Il gate corretto supera una ripetizione mirata di quattro minuti e i
due boot che richiedono avanzamento dei timer AP dopo il cleanup.
[Risultati, immagini e limiti](validation/consolidamento.md#cpu-e-scheduler).

Risultati precedenti con immagini e limiti propri nelle validazioni
[AP nel boot ordinario](validation/consolidamento.md#validazione-dei-backend-x86),
[scheduler e shell](../kernel/scheduler/documentation/validation.md),
[consolidamento e build Meson](validation/consolidamento.md), [processi](validation/processi.md) e [protezioni](validation/sicurezza.md).
L'elenco storico dei checkpoint è nella versione di questa pagina a `efecb8b`.
