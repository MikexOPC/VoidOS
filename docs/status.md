# Stato di VoidOS — 18 novembre 2026

## Immagine ordinaria (`ordinary`, Meson)

- Il loader UEFI carica il kernel ELF. Il kernel configura paging con NX e
  WP, GDT/TSS/IST, allocatore fisico, heap e interrupt. In QEMU è stato
  inizializzato anche il framebuffer GOP.
- In QEMU con quattro vCPU, il core di avvio (BSP) avvia tre core secondari
  (AP). Tutti e tre rispondono al PING inter-CPU e attendono parcheggiati.
  Lo scheduler gestisce la sessione PS/2 sul BSP con un timer di preemption
  da 10 ms (vettore D1h). I task della sessione partono con gli interrupt
  attivi e il timer li sospende anche fuori dalla finestra di attesa.
- Con SMEP e SMAP disponibili, il BSP esegue un ELF Rust incorporato in
  ring 3 con tabelle di paging dedicate. Il programma stampa via syscall,
  termina e viene rilasciato prima di `KERNEL_READY`. UMIP viene abilitato
  se la CPU lo supporta. Lo stack delle syscall ha due guard page.
- Con PS/2, `ordinary` apre la shell con tastiera e mouse. `bridge status`
  interroga il servizio residente e `ap <n>` invia un job fidato ad AP1.
  `exit` chiude la sessione e arresta il BSP. Senza PS/2, dopo
  `KERNEL_READY` il BSP entra in `KERNEL_IDLE` con interrupt abilitati.
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
  Il prossimo passo di S1 è il [dispatcher permanente sul BSP](design/scheduler-produzione.md#prossimo-passo-di-s1).
  B2-1 separa preparazione di timer/watchdog ed esecuzione dei servizi.
  Restano l'ingresso permanente e il cleanup PS/2 con dispatcher attivo.
  Poi restano la preemption ordinaria sugli AP e i limiti fissi di otto
  task e quattro sottoscrizioni IRQ. L'heap è limitato a 16 MiB e c'è
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

Su `a947255`, kernel Meson `ordinary` con quattro vCPU e loader UEFI
già compilato nel worktree: `plan_parity` 1/1 e tre boot QEMU/OVMF,
uno per avvio ordinario, bridge nella shell e idle senza PS/2.
Nessun test di durata o prova fisica in questo passaggio.

Verifica iniziale su `efecb8b`: test host del loader completati,
`plan_parity` 1/1 e un boot QEMU/OVMF 1/1 di `ordinary` con quattro vCPU.
Il log mostra tre AP parcheggiati e tre ACK, l'uscita di init e
`KERNEL_READY`. Il runner ha letto SMEP, SMAP e UMIP su tutte e quattro
le vCPU. Stress e profili separati non sono stati rieseguiti in questa prova.

Risultati precedenti con immagini e limiti propri nelle validazioni
[AP nel boot ordinario](validation/consolidamento.md#validazione-dei-backend-x86),
[scheduler e shell](../kernel/scheduler/documentation/validation.md),
[consolidamento e build Meson](validation/consolidamento.md), [processi](validation/processi.md) e [protezioni](validation/sicurezza.md).
L'elenco storico dei checkpoint è nella versione di questa pagina a `efecb8b`.
