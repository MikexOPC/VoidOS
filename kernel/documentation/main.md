# kernel/main.c: sequenza di avvio e profili

22 ottobre 2026. Fotografia del file al commit `09d1a35` (316 righe, 61 direttive
`#if/#ifdef/#elif/#else/#endif`), primo passo di FW-009 in [future-work](../../docs/future-work.md).
La sequenza descritta resta valida dopo lo spezzamento applicato (in fondo).

## Due fasi e due stack

Il loader salta a `kernel_main` sullo stack fornito dal loader. Il kernel prepara il
proprio stack principale e prosegue in `kernel_continue`, che non ritorna mai: termina
con `x86_kernel_stop` (KERNEL_READY) oppure con un panic. Ogni passo che fallisce
chiama `kernel_panic` con un codice proprio, riportato nella tabella.

### Fase A — `kernel_main`, stack del loader

| # | Passo | Prerequisito | Panic |
|---|---|---|---|
| A1 | `kernel_capture_contract`: copia e valida il contratto v5 | nessuno (seriale gia pronta) | BOOT_CONTRACT_INVALID |
| A2 | GPU PCI dal contratto: `kernel_vgpu_pci_probe`, stampa le regioni | A1 | VGPU_METADATA_INVALID |
| A3 | `x86_exception_initialize`: IDT delle eccezioni | A1 | KERNEL_IDT_FAILED |
| A4 | `kernel_inventory_memory`: mappa UEFI | A1 | MEMORY_MAP_INVALID |
| A5 | `fb_handoff_memory_valid` e stampa del framebuffer | A4 | FRAMEBUFFER_RESOURCES_INVALID |
| A6 | `kernel_bootstrap_memory_prepare`: allocatore fisico, arena, finestra | A4 | (interno) |
| A7 | `fb_mapping_geometry`: piano delle tabelle del framebuffer, nessuna allocazione | A6 | FRAMEBUFFER_PLAN_INVALID |
| A8 | `x86_kernel_segments_initialize`: GDT, TSS, IST | A6 | KERNEL_DESCRIPTORS_FAILED |
| A9 | `x86_kernel_main_prepare` e `x86_kernel_main_enter` | A6, A8 | MAIN_STACK_PREPARE_FAILED |

### Fase B — `kernel_continue`, stack principale

| # | Passo | Prerequisito | Panic |
|---|---|---|---|
| B1 | `x86_kernel_main_retire`: revoca lo stack del loader | A9 | MAIN_STACK_RETIRE_FAILED |
| B2 | `kernel_scheduler_run`: profilo scheduler di base | B1 | SCHEDULER_FAILED |
| B3 | RSDP ACPI valida o assente | A1 | ACPI_ROOT_INVALID |
| B4 | stato persistente nell'arena coerente con il contratto | A6 | PERSISTENT_STATE_INVALID |
| B5 | inventario paging (`x86_paging_inventory_capture/verify`) | B1 | PAGING_INVENTORY_INVALID |
| B6 | `kernel_acpi_initialize`: directory ACPI | B5 | ACPI_DIRECTORY_INVALID |
| B7 | HPET (solo con ACPI) | B6 | HPET_DISCOVERY_INVALID |
| B8 | `kernel_madt_initialize`: topologia MADT | B6 | MADT_TOPOLOGY_INVALID |
| B9 | APIC: preflight, probe MMIO | B8 | APIC_PREFLIGHT_INVALID, APIC_MMIO_INVALID |
| B10 | `x86_irq_initialize`, `x86_apic_quiesce` | B9 | IRQ_RETURN_INVALID, APIC_MASK_INVALID |
| B11 | `x86_uart_irq_run`: IRQ della seriale, mapping LAPIC | B10 | UART_IRQ_INVALID |
| B12 | video e timer, ordine dipendente dal profilo (vedi sotto) | B11 | FRAMEBUFFER_MAPPING_FAILED, VGPU_MAPPING_FAILED, LAPIC_TIMER_INVALID |
| B13 | fine: KERNEL_READY e `x86_kernel_stop`, oppure fault di prova | tutto | — |

B12 nel profilo ordinario: watchdog/timer LAPIC, poi framebuffer e GPU VirtIO. Nei
profili console (`KERNEL_CONSOLE_SHELL`, `KERNEL_TEST_CONSOLE_ACTIVE`) l'ordine si
inverte: framebuffer e GPU prima del timer, perche la sessione di input parte dentro
`x86_watchdog_kernel_start` (driver LAPIC, `x86_ps2_session_run`) e deve gia trovare la
console; dopo la sessione si verifica l'inventario video. Ogni passo che usa ACPI e
saltato quando la RSDP e assente.

## Profili che cambiano la sequenza

| Macro (opzione di build) | Dove interviene | Effetto |
|---|---|---|
| `KERNEL_TEST_PS2_DECODER` | dopo B1 | verifica ABI del decoder |
| `KERNEL_TEST_PCI_ECAM` (+ topologia, routing, attivazione VT-d) | dopo B6 | probe ECAM; dentro: topologia, routing, attivazione |
| `KERNEL_TEST_IOMMU_DISCOVERY` | dopo B6 | discovery DMAR |
| `KERNEL_TEST_PS2_CONTROLLER` | dopo B6 | probe del controller PS/2 |
| `KERNEL_TEST_IOMMU_REGISTERS` (+ `_DOMAIN`, `_QI`) | sostituisce B12 | registri VT-d, poi dominio oppure QI |
| `KERNEL_TEST_VGPU_INITIALIZATION` | sostituisce B12 | inizializzazione GPU VirtIO |
| `KERNEL_CONSOLE_SHELL`, `KERNEL_TEST_CONSOLE_ACTIVE` | dentro B12 | ordine video/timer invertito |
| `KERNEL_TEST_REGION_FAULT`, `_MAIN_FAULT`, `_WINDOW_FAULT`, `_CPU_FAULT`, `KERNEL_TEST_PANIC` | sostituisce B13 | fault o panic intenzionali |

Le combinazioni ammesse sono controllate da `kernel/scripts/build/profiles.ps1`: i
diagnostici IOMMU, ECAM e VirtIO restano isolati dai profili di input e console.
Per gli `#ifdef` dentro i moduli (scheduler, PS/2, console) vedere i rispettivi documenti.

## Problemi della forma attuale

- La sequenza di produzione (A1–A9, B1–B13) e mescolata a 15 macro di profilo; la catena
  `#if REGISTERS / #elif VGPU / #else` di B12 nasconde l'ordine del profilo ordinario.
- Le inclusioni dei probe di test sono in testa al file, sotto macro: corretto per
  l'audit dei simboli, ma ogni nuovo diagnostico aggiunge un blocco in tre punti.
- Codici di panic e stampe di diagnostica (contratto, framebuffer, paging) sono in linea:
  rendono lunghi passi che concettualmente sono una sola chiamata.

## Struttura applicata (22 ottobre 2026)

    kernel/main.c              42 righe, nessuna direttiva #if: A8-A9, poi
                               ganci e passi della fase B nell'ordine sopra
    kernel/startup/startup.h   stato condiviso (KERNEL_STARTUP) e firme
    kernel/startup/early.c     A1-A7
    kernel/startup/platform.c  B2-B11, con il gancio ACPI dopo B6
    kernel/startup/devices.c   B12 (ordine ordinario o console)
    kernel/startup/profile.c   tutti i ganci dei profili diagnostici e B13

Il codice e stato spostato, non riscritto: stesse chiamate, stesso ordine, stessi
codici di panic. Le direttive dei profili di prova stanno solo in `profile.c`; in
`devices.c` restano quelle dei profili console, che sono prodotto e non prova.

Prove:
- 59 combinazioni di profilo compilano (tutte le opzioni di `build.ps1`, compresi i
  27 valori dei fault), `kernel/build/startup-matrix/summary.log`.
- Mappe dei simboli confrontate con le build precedenti dello stesso albero
  (attivazione, routing, topologia, interattivo): nessuna funzione persa, aggiunte solo
  quelle di `startup/`. Nel profilo QI entrano anche timer, framebuffer e VirtIO,
  linkati ma mai eseguiti perche il gancio diagnostico sostituisce B12.
- Suite host complete; audit dei simboli dell'immagine ordinaria PASS.
- Boot, dieci per profilo, tutti exit 0: ordinario, mouse+shell, interattivo,
  attivazione VT-d, QI, inizializzazione VirtIO, decoder e controller PS/2.
- La suite dei fault (`test/kernel/firmware.ps1`) fallisce al primo scenario anche con
  l'immagine costruita dal commit precedente: il controllo della memoria
  dell'inventario paging e anteriore alla mappatura del framebuffer nel boot. Il
  problema esisteva gia; i rami di fault di B13 compilano ma non sono stati avviati.
