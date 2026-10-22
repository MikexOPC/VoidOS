# Verifica mapping VirtIO GPU — 19 ottobre 2026

Perimetro: una GPU, massimo16 pagine UC, tre tabelle possedute con lease
pinned e guardie, convivenza con framebuffer e LAPIC. Contratto boot v5
invariato. [Design e fonti](design.md). Non e ancora un driver scanout.

## Prove host e build

| Controllo | Risultato | Evidenza in kernel/build/ |
| --- | --- | --- |
| Piano C/Rust ABI, ASan C | 12.315 verifiche PASS | virtio-page-plan-host.log |
| Piano Rust debug/release, overflow checks, Clippy | PASS | virtio-page-plan-host.log |
| Runner piano Windows PowerShell5.1 | PASS | virtio-page-plan-ps51.log |
| Bind/unbind, allocatore e lease reali, hardware mock | 793.400 verifiche,100 cicli PASS | virtio-mapping-final-host.log |
| Parser PCI C/Rust | 477.762 verifiche PASS | virtio-mapping-discovery-host.log |
| Lettura selettiva, finestra indiretta mai letta | 230 verifiche PASS | virtio-mapping-discovery-host.log |
| Acquisizione UEFI, accounting mock | 13.024 verifiche PASS | virtio-mapping-uefi-host.log |
| Build loader e suite DLL/Rust | PASS | virtio-mapping-safe-loader.log |
| Build kernel diagnostico/ordinario | PASS | virtio-mapping-final-kernel.log, virtio-mapping-final-ordinary-kernel.log |
| Suite host kernel completa sullo stato finale | PASS | virtio-mapping-final-core-host.log |
| Regressioni paging/framebuffer/console | PASS | virtio-mapping-compatible-paging-host.log, virtio-mapping-compatible-framebuffer-host.log, virtio-mapping-console-host.log |

MSVC /W4 /WX e Rust con warning come errori. cppcheck senza diagnostiche
bloccanti error/warning/performance/portability; restano note style e
informazioni sugli include della toolchain. ASan strumenta C host, non
Rust o RAM fisica. Valgrind non disponibile/eseguito; copertura linee e
branch N/D. Non si deduce zero leak fisici da ASan.

I runner legacy paging/framebuffer invocati da Windows PowerShell5.1
falliscono sulla redirezione dell'XML cppcheck a stderr, dopo la compilazione
e le prove host. I log `virtio-mapping-final-paging-host.log` e
`virtio-mapping-final-framebuffer-host.log` conservano il tentativo fallito.
Gli adattatori ora avviano cppcheck con stderr/stdout direttamente su file
tramite Start-Process, conservando verifica ExitCode e severita XML.
Le esecuzioni finali passano con5.1 senza cambiare policy persistenti:
otto gruppi paging per100 cicli e suite framebuffer lifecycle/pixel/drawing.
Le esecuzioni precedenti conPowerShell7 passano anch'esse. Il nuovo mapper
e il piano pagine passano anche con5.1.

I test mapper usano il vero allocatore fisico con128 pagine host allineate,
ma CPU, PCI e accesso alle PTE sono mock. Coprono output/owner copiato,
lease stantii, alias, flag alterati, registrazione duplicata, esaurimento
dopo allocazione parziale, errore capture/borrow/invalidation e retry dopo
revoca fallita. Le risorse ancora raggiungibili restano trattenute.

## Campagna QEMU finale

QEMU11.1.0 build v11.1.0-12130-ge470268ff4, q35/TCG,128MiB, una CPU,
OVMF e ESP isolata per boot. Il test possiede e termina soltanto la propria
VM, usa deadline finite e controlla marker, fault e budget. Device
virtio-vga moderno, vendor1af4/device1050; fallback con VGA standard.

Il riscontro ferma le vCPU tramite QMP e legge solo RAM ordinaria per
verificare tutte le512 entry di ciascuna delle tre tabelle e PML4[226].
Le quattro capability PCI derivano da due letture selettive stabili e
sono confrontate con parser host e log guest. Nell'emulatore l'unione
contiene tre pagine MMIO, pur essendo il limite progettuale16.
Foglie supervisor RW/NX/UC, guardie e entry inutilizzate assenti; nel
fallback nessun ramo GPU pubblicato. Nessun registro GPU letto via MMIO.

Ogni boot esercita movimento mouse, input tastiera ed Esc/exit fino a
KERNEL_READY, con budget scheduler ripristinato. Il profilo diagnostico
controlla unbind/rebind con rilascio completo e nuovi ID; il profilo
ordinario non contiene il simbolo x86_vgpu_mapping_probe nel MAP.

Diagnostica finale:10/10 boot consecutivi PASS, manifest
`kernel/build/virtio-mapping-firmware-2f7bb894308444a7bf9cfab24b40edb6/results.json`.
SHA256 kernel diagnostico:
`CC4F93265398E2C9E352B8FB585FE7B349D3F0A9640B7AF0886B84EABB95B6AF`.
SHA256 loader condiviso:
`C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.

Ordinario finale:10/10 boot consecutivi PASS, manifest
`kernel/build/virtio-mapping-firmware-992120afb3744745a426131698991b99/results.json`.
SHA256 kernel ordinario:
`DC88BF41436C7CF09D79C841A9AC0772AB5F7E0EE7B36148B1B5B5ABF20438E9`.

Fallback VGA finale:10/10 boot consecutivi PASS sulla stessa immagine
ordinaria e sullo stesso loader, manifest
`kernel/build/virtio-mapping-firmware-c360f33b2f9945f08c044b229e0a24e4/results.json`.
Totale finale30 boot,10 consecutivi per ciascuno dei tre profili, senza
fault inattesi; mouse consumato e cleanup input verificati in tutti.

Controllo finale di struttura:48 sorgenti di discovery, acquisizione,
mapping, autorita paging e relativi test; file entro400 righe, massimo
cinque sorgenti diretti per cartella, include/link risolti e LF. Gli
adattatori modificati sono entro400 righe; build372, main273. Diff senza
whitespace error nei percorsi coinvolti. Manifest:
`kernel/build/virtio-pci-structure.json`. Nessuna modifica a reuse/.

Le campagne precedenti con prefisso virtio-mapping-safe e le prime campagne
senza safe restano tentativi storici: precedono rispettivamente il rifiuto
dell'intera Capacity video e la lettura selettiva PCI. Non sostituiscono
le prove finali. Le nuove verifiche non misurano FPS o presentazione SDL.

## Correzioni emerse durante implementazione e revisione

- Il budget del renderer ignorava le tre tabelle GPU: incluso nel budget
  atteso, conservando la scansione completa e la verifica dei lease.
- Le pagine GPU devono essere disgiunte dall'intera Capacity del framebuffer,
  compresa la parte non mappata per il modo corrente. Rifiuto prima del bind
  e durante la rivalidazione dell'inventario; test del margine non usato e
  della sovrapposizione parziale a pagina gia mappata.
- L'avvio GPU dopo il watchdog interattivo era raggiungibile solo dopo
  l'uscita input: spostato prima del ciclo delle task.
- Alcune operazioni Rust lasciavano simboli panic overflow non risolti:
  aritmetica esplicita checked/saturating e accessi bounded, senza togliere
  overflow checks o aggiungere runtime fittizi.
- VirtIO PCI_CFG type5 espone una finestra indiretta al byte16 della
  capability: leggere tutti i256 byte puo accedere al BAR e produrre effetti
  collaterali. Loader, controllo PCI nativo e harness leggono solo header e
  metadati. Il test specifico vieta ogni accesso alla finestra. Fonte:
  [VirtIO1.2 §4.1.4.9](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).
  Nessun effetto collaterale reale e stato osservato nella campagna iniziale;
  il difetto e stato identificato tramite specifica e implementazione QEMU.
- Fixture worker preesistente priva del simbolo PS/2 consume: aggiunto uno
  stub che fallisce se invocato, senza modificare il driver PS/2.

## Limiti e passo successivo

Nessuna lettura/scrittura dei registri MMIO GPU, reset, negoziazione feature,
virtqueue, DMA, interrupt GPU, scanout o cursore hardware. Nessun test su
hardware fisico, AP/SMP, hot-unplug, PCID o IOMMU. BSP con mutazioni IF=0;
IF=0 non e un lock SMP. L'accesso PCI nativo supporta segmento0.

Durante il funzionamento ordinario le tre tabelle GPU restano pinned:
sono risorse persistenti intenzionali. Il rollback diagnostico dimostra
il ritorno al budget originale e il rebind con nuovi ID; il controllo del
budget scheduler a exit riguarda le sue risorse temporanee. Non equivale
al rilascio dell'intero kernel. Console e cursore restano sul framebuffer
firmware; nessuna nuova garanzia di fluidita o60FPS.

Il prossimo blocco e il protocollo di inizializzazione/reset e feature
negotiation, prima di code e DMA. Un fallimento dopo takeover non consente
di presumere automaticamente valido il vecchio framebuffer GOP.

Aggiornamento successivo dello stesso giorno: il
[blocco diagnostico di inizializzazione](../../initialization/documentation/validation.md)
verifica ora reset e negoziazione, separati dall'avvio ordinario. Le
campagne e i limiti del presente report restano quelli del mapping.
