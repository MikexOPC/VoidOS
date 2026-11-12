# Validazione del consolidamento

Versione completa precedente in `docs/status.md`, `docs/review/verified-state.md`, `docs/review/status-2026-10-22.md`, `docs/handoff/codex-status.md`, `docs/handoff/k4-remaining.md`, `docs/design/k1-higher-half.md`, `docs/design/project-direction.md` e `docs/design/k6-design.md` (build), al commit `efecb8b`.

La validazione di chiusura dell'immagine unica è aperta. I conteggi
seguenti appartengono alle immagini e alle revisioni indicate nei
report. Lo [stato](../status.md) descrive l'immagine ordinaria attuale.

## Boot, memoria e dispositivi

| Checkpoint | Prova e perimetro |
| --- | --- |
| Loader, 2 ottobre | EFI di 54.272 byte, SHA-256 `FF6EB0D78AF37328394FE015171260577DB225E4CC23BC29996AF933C1AC10C1`. Test host C/C++ e Rust, 5000 mutazioni ELF e 512 sequenze di errore delle tabelle. QEMU/EDK2 10/10 casi loader. Il kernel ordinario non veniva eseguito e il cambio CR3 era fuori prova. |
| Dispatcher, 8 ottobre | 10 boot positivi e 9 negativi su QEMU TCG/OVMF. Scheduler cooperativo BSP, IRQ durante il worker, epoca e idle riusato. [Report](../../kernel/scheduler/idle/documentation/dispatcher-validation.md). |
| Boot e console, snapshot 22 ottobre | Bootstrap di memoria separato con 10 boot per profilo e 23 scenari diagnostici. Shell e input PS/2 in profili separati, console grafica GOP e prove VirtIO per componenti. [Bootstrap](../../kernel/bootstrap/documentation/memory.md), [PS/2](../../arch/x86/kernel/drivers/input/ps2/documentation/validation.md), [shell](../../kernel/shell/documentation/shell.md), [presentazione VirtIO](../../kernel/drivers/video/virtio/resources/documentation/presentation-validation.md). |
| K1, revoca dello slot basso | Dieci boot dedicati e negativo con traduzione scaldata prima di MOV CR3. Kernel 23/23, campagne brevi 12/12, `v2c` 10/10 e 37 suite host complessive con retry mirato. Le pagine del loader restano riservate. [Report](../../arch/x86/mm/paging/retirement/documentation/validation.md). |
| K2, lock, pool e heap | Ticket lock con 140.000 acquisizioni FIFO host. Integrazione ordinaria 17/17 smoke, più dieci boot dell'ELF heap ricompilato. Contesa host con thread e ASan. Il runtime guest di questo checkpoint era BSP. [Report](../../kernel/sync/documentation/k2-integration.md). |

Per le verifiche puntuali dei driver del checkpoint di ottobre valgono
i report di [ECAM](../../arch/x86/kernel/drivers/bus/pci/ecam/documentation/validation.md),
[handoff PCI](../../arch/x86/boot/uefi/pci/documentation/validation.md),
[discovery VT-d](../../arch/x86/kernel/drivers/memory/iommu/intel/discovery/documentation/validation.md),
[domini VT-d](../../arch/x86/kernel/drivers/memory/iommu/intel/domains/documentation/validation.md)
e [code DMA VirtIO](../../kernel/drivers/video/virtio/queues/documentation/validation.md).

## CPU e scheduler

- **K3.** GS, clock delle attese, identità e risorse AP precedono
  ammissione, sigillo e avvio. K3-F aveva un solo core OS attivo anche
  con quattro vCPU configurate. K3-H provò la root inattiva e il
  rollback. K3-I avviò realmente BSP e tre AP e verificò il panic
  coordinato con origine BSP e AP. Chiusura 80/80 su QEMU/OVMF xAPIC,
  30 boot ordinari/console e 50 panic intenzionali, più una sessione
  continua di circa 301 s. [K3-F](../../arch/x86/kernel/cpu/percpu/documentation/k3f-validation.md),
  [K3-H](../../arch/x86/kernel/cpu/percpu/documentation/k3h-validation.md),
  [chiusura K3-I](../../arch/x86/kernel/cpu/percpu/documentation/k3i-runtime-validation.md).
- **K4-A/B.** PING agli AP nel boot ordinario, 40 boot su 1/2/4/8
  CPU. Shootdown prima del riuso dei frame, 40/40 a quattro CPU;
  ACK mancante con risorsa trattenuta nel caso diagnostico.
  [K4-A](../../arch/x86/kernel/cpu/percpu/documentation/k4a-validation.md),
  [K4-B](../../arch/x86/kernel/cpu/percpu/documentation/k4b-validation.md).
- **K4-C.** Il core Rust fu verificato in host e la preemption timer
  nei profili BSP/AP. `ap-task-four` provò otto task su quattro AP
  con BSP supervisore, 50 boot positivi e 20 arresti attesi per
  quiescenza incompleta. `v2c` passò 10/10 a quattro CPU e
  un soak VirtIO/VT-d di 301 s. Il comando `ap` resta della shell
  separata e i driver restano sul BSP.
  [Report K4-C](../../kernel/scheduler/documentation/k4c-validation.md).

## Build e runner

| Passo | Risultato storico |
| --- | --- |
| Pilota Meson | Su tre ripetizioni dell'editor di riga, mediana build+test 3,33 s PowerShell, 3,24 s Meson dopo setup e 2,17 s senza modifiche. Setup 7,82 s. Rebuild selettivo per C e Rust verificato. |
| M1, runner Rust | Parità sulla stessa immagine per `k5-copy` (30/30 per runner) e sei negativi (18/18 per runner). Coperti timeout, monitor, uscita e chiusura della VM posseduta. Dal registro, 7/7 campagne e 28 boot su immagini ricompilate. Windows, TCG/q35/OVMF. |
| M2, grafo Meson | Undici profili con stessi comandi, ordine e immagini byte per byte a parità di directory dopo `/Brepro`. Verificate invalidazione C, Rust e trampolino e nessuna ricompilazione senza modifiche. Quattro campagne Meson, 20 boot, più dieci boot singoli dei profili K6. |
| 12 novembre | A `efecb8b`, nove campagne ordinarie passano a Meson e 23 su 121 lo usano. Verifica documentale nel worktree con build separata: test host del loader completati, `plan_parity` 1/1, `ordinary` 1/1 con quattro vCPU e tre AP parcheggiati. |

Copertura, hardware fisico, build a un comando, CI e stress da trenta
minuti dell'immagine unica restano fuori dalle prove citate.

## Evidenze e limiti dei checkpoint

- Per il loader del 2 ottobre, i log erano nella build ignorata
  `firmware-test-9966cce985644e119bb26c4b8c259b44/`. Il preflight
  era verificato in QEMU, mentre ExitBootServices era provato con
  mock host. Il comportamento su alias di allocazione ambiguo
  trattiene la pagina per evitare di liberare la risorsa di un owner vivo.
- La validazione del 22 ottobre raccoglieva anche la correzione del
  rollover del timer, con 32 boot/sessioni finali e budget ripristinato.
  Per la console PS/2 riporta dieci boot shell, dieci ordinari e 612
  pressioni di stress. I dettagli e gli hash restano nei report dei
  moduli. [Rollover](../../arch/x86/kernel/timer/hpet/watchdog/documentation/idle/rollover.md),
  [verifica input](../../kernel/drivers/input/ps2/documentation/structure.md).
- Nei checkpoint K3-A–H, i casi host di GS, clock, identità, risorse e
  lock non provavano l'avvio di un AP. Il test di riserva bassa K3-G
  passò 10/10 dedicati, 10/10 ordinari, negativo 1/1 e console VirtIO/VT-d
  1/1. K3-H passò 10/10 preparazioni, rollback 1/1, 10/10 ordinari e
  10/10 console, ancora senza avviare AP. [K3-G](../../arch/x86/kernel/cpu/percpu/documentation/k3g-validation.md).
- Il pilota del planner misurò tre coppie di build pulite con mediane
  di 27,50 s PowerShell e 34,48 s con `-UseRustPlan`. Su nove prove
  del solo piano, 315 ms e 23 ms rispettivamente, con planner Rust già
  compilato. Sono misure sull'host del checkpoint.
- Il confronto M1 dei parser usò 5012 log (12 reali, gli altri mutati)
  senza differenze. La VM estranea rimase intatta nelle prove di
  chiusura. M2 verificò parità di piano, PE/ELF e layout. Restano
  dipendenze locali negli script e profili diagnostici legacy.
  [Guida strumenti e prove](../../scripts/README.md).

Le note operative storiche su pulizia degli artefatti e campagne
manuali restano nella [guida agli script](../../scripts/README.md).
Il [registro del debito](../future-work.md) raccoglie i limiti video
e IOMMU ancora da valutare.
