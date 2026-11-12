# Stato di VoidOS — 12 novembre 2026

## Immagine ordinaria (`ordinary`, Meson)

- Il loader UEFI carica il kernel ELF. Il kernel configura paging con NX e
  WP, GDT/TSS/IST, allocatore fisico, heap e interrupt. In QEMU è stato
  inizializzato anche il framebuffer GOP.
- In QEMU con quattro vCPU, il core di avvio (BSP) avvia tre core secondari
  (AP). Tutti e tre rispondono al PING inter-CPU e attendono parcheggiati.
  Lo scheduler cooperativo esegue i task di avvio sul BSP.
- Con SMEP e SMAP disponibili, il BSP esegue un ELF Rust incorporato in
  ring 3 con tabelle di paging dedicate. Il programma stampa via syscall,
  termina e viene rilasciato prima di `KERNEL_READY`. UMIP viene abilitato
  se la CPU lo supporta. Lo stack delle syscall ha due guard page.
- Dopo `KERNEL_READY` il BSP si arresta con gli interrupt disabilitati.
  La build e il boot sono descritti nel [README](../README.md).

## Profili separati

| Profilo o caso | Capacità provata |
| --- | --- |
| `k6-shell`, `v2c` | Tastiera e mouse PS/2 e shell interattiva. `bridge status` interroga un servizio residente in ring 3 nel profilo PS/2. `v2c` prova la console VirtIO con un dominio VT-d per la GPU e il comando `ap <n>` per un job fidato alla volta su AP1. |
| `meson-bsp-periodic`, `ap-task-four` | Preemption da timer sul BSP nel primo caso. Nel secondo, otto task kernel fidati distribuiti su quattro AP, con affinità fissa. |
| `k6-process`, `k6-process-smp` | Casi diagnostici di esecuzione in ring 3, fault utente e IPC tra due processi, con cleanup delle risorse. |

Le configurazioni sono nel [registro delle campagne](../scripts/test/campaigns.json).
I profili diagnostici compilati solo da `build.ps1` saranno ritirati man mano
che le loro prove trovano posto nei test host o nell'immagine unica.

## Limiti e prossimo passo

- Manca un'immagine persistente che riunisca shell, processi utente e
  scheduler preemptive su tutte le CPU. Restano limiti fissi di otto task
  e quattro sottoscrizioni IRQ. L'heap è limitato a 16 MiB e c'è un solo
  processo attivo alla volta.
- Mancano driver a blocchi, VFS, filesystem del kernel ed esecuzione di ELF
  da file. La lettura dell'ELF del kernel avviene nel loader tramite UEFI.
- Mancano sorgente di entropia e stack canary del kernel. W^X per sezione
  dell'intera immagine e overflow reale dello stack syscall sono non
  verificati. Il cancello «base solida» resta aperto. Hardware fisico non
  verificato per il percorso descritto qui.
- Prossimo passo del consolidamento dopo il riordino della documentazione
  e il commit del lavoro GUI: dividere `process.c`, poi integrare a gruppi le
  suite host in `meson test` e collegare build, test e boot ordinario alla CI.

## Evidenze

Verifica di questa pagina su `efecb8b`: test host del loader completati,
`plan_parity` 1/1 e un boot QEMU/OVMF 1/1 di `ordinary` con quattro vCPU.
Il log mostra tre AP parcheggiati e tre ACK, l'uscita di init e
`KERNEL_READY`. Il runner ha letto SMEP, SMAP e UMIP su tutte e quattro
le vCPU. Stress e profili separati non sono stati rieseguiti in questa prova.

Risultati precedenti con immagini e limiti propri nelle validazioni
[AP nel boot ordinario](../arch/x86/kernel/cpu/percpu/documentation/k4a-validation.md),
[scheduler e shell](../kernel/scheduler/documentation/k4c-validation.md),
[processi e build Meson](design/k6-design.md) e [protezioni](handoff/security-plan.md).
L'elenco storico dei checkpoint è nella versione di questa pagina a `efecb8b`.
