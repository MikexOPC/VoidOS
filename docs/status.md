# Stato di VoidOS — 14 novembre 2026

## Immagine ordinaria (`ordinary`, Meson)

- Il loader UEFI carica il kernel ELF. Il kernel configura paging con NX e
  WP, GDT/TSS/IST, allocatore fisico, heap e interrupt. In QEMU è stato
  inizializzato anche il framebuffer GOP.
- In QEMU con quattro vCPU, il core di avvio (BSP) avvia tre core secondari
  (AP). Tutti e tre rispondono al PING inter-CPU e attendono parcheggiati.
  Lo scheduler cooperativo gestisce la sessione PS/2 sul BSP.
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

- Shell e processo utente convivono in `ordinary`. Il dispatcher gira a
  sessioni sul BSP e l'idle finale non ospita servizi. Manca la preemption
  ordinaria su tutte le CPU. Restano limiti fissi di otto task
  e quattro sottoscrizioni IRQ. L'heap è limitato a 16 MiB e c'è un solo
  processo attivo alla volta.
- Mancano driver a blocchi, VFS, filesystem del kernel ed esecuzione di ELF
  da file. La lettura dell'ELF del kernel avviene nel loader tramite UEFI.
- Mancano sorgente di entropia e stack canary del kernel. W^X per sezione
  dell'intera immagine e overflow reale dello stack syscall sono non
  verificati. Il cancello «base solida» resta aperto. Hardware fisico non
  verificato per il percorso descritto qui.
- Il backend processi è stato diviso. La CI ha un workflow per suite host
  e boot ordinario. Restano la migrazione delle suite host in `meson test`
  e la scelta del [dispatcher permanente](design/scheduler-produzione.md).

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
