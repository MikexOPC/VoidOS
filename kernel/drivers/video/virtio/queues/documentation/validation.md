# Code VirtIO GPU: verifica del 19 ottobre 2026

Esito: blocco diagnostico verificato su QEMU fidato, BSP, IF=0.
[Contratto, fonti e scelta IOMMU](design.md).

## Cosa funziona

Due split queue da8 descrittori, buffer dedicati su quattro pagine pinned
della finestra memoria esistente, richieste GET_DISPLAY_INFO fenced e
risposte controllate.256 richieste sequenziali reali per boot diagnostico.
La cursor queue e inizializzata ma non riceve ancora comandi.
Nessuna nuova tabella o modifica all'allocatore/inventario globale.

Il reset confermato precede il rilascio del blocco RAM; il mapping registri
e poi revocato. Se il reset fallisce, i buffer restano posseduti/pinned.
Bounds e ownership vengono controllati; queste verifiche non isolano DMA
ostile. IOMMU rinviata esclusivamente nel profilo emulato fidato concordato.

Il build switch e `-TestVgpuQueues`: deriva il profilo diagnostico isolato
di inizializzazione. I nuovi sorgenti comuni, backend e probe vengono
linkati soltanto in questa variante. Il MAP ordinario non contiene simboli
`kernel_vgpu_queue_*` o `x86_vgpu_queue_*`.
La ESP ordinaria non e stata installata o aggiornata da questo blocco.

## Test host eseguiti

Runner: `kernel/drivers/video/virtio/queues/scripts/test.ps1`.

| Prova | Risultato finale |
| --- | --- |
| Protocollo C con Rust ABI reale |641217 controlli,70000 completion simulate |
| Trasporto nativo con MMIO su RAM sostitutiva |59600 controlli |
| Lifecycle diagnostico con risorse/reset mock |88600 controlli,800 sessioni |
| Policy Rust |4 test debug e4 release, overflow checks attivi |
| Clippy |policy e gateway FFI passano con warning bloccanti |
| Regressione inizializzazione |89300 controlli protocollo,61900 trasporto |

I contatori non rappresentano altrettanti scenari distinti o coverage.
Le70000 completion coprono il wrap16bit; i test negativi comprendono ID,
lunghezza, fence e geometria errati, clock fermo, timeout, errori IO a ogni
posizione esercitata, alias e limiti notify. Il lifecycle verifica anche
reset prima del rilascio, fallimento del reset con buffer trattenuti,
errore di apertura, allocazione, capture, start, exchange e verifica finale.

C host compilato MSVC con /W4 /WX e ASan, Rust con warning bloccanti.
Nessun errore ASan osservato nelle tre suite C. I test del trasporto
sostituiscono PCI, clock e registri; il lifecycle sostituisce il device e
le API memoria. Non dimostrano isolamento DMA o correttezza hardware da soli.
Nessun heap nuovo nelle code; accounting fisico verificato separatamente
in QEMU. Valgrind non eseguito; coverage linee/branch N/D.

Runner verificato anche con Windows PowerShell5.1. Log:

- `kernel/build/virtio-queues-host-final.log`
- `kernel/build/virtio-queues-ps51.log`
- `kernel/build/virtio-queues-init-host.log`

Cppcheck2.22.0: nessuna diagnostica error/warning/performance/portability;
restano10 suggerimenti style e22 information (include toolchain e analisi
delle configurazioni), conservati in `kernel/build/virtio-queues-host/cppcheck.xml`.
Non vengono descritti come zero diagnostiche totali.

## Immagini finali

Diagnostico: `kernel/build/virtio-queues-image/kernel.elf`, SHA256
`EF6946D744C29C5AA9EF23A9DC63631CFE0C0F4EC7B9C68D400D3BF5B7B8957D`.

Ordinario console/mouse interattivo:
`kernel/build/virtio-queues-ordinary/kernel.elf`, SHA256
`B5BA8CE454EB097B9B6DC16CE5FFF29697B1489EA7DC108245FE58853D054ED1`.

Loader invariato: `arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI`, SHA256
`C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.

## Campagne finali QEMU

QEMU11.1.0 build locale, q35/TCG, CPU max,1 vCPU,128MiB, OVMF e ESP
privati per boot. Nessun passthrough. Hash verificati nella copia ESP,
deadline host e terminazione limitata alla VM posseduta. Campagne in serie.

| Profilo | Risultato | Evidenza sotto kernel/build/ |
| --- | --- | --- |
| Diagnostico con VirtIO |10/10 consecutivi,2560 richieste reali totali |virtio-initialization-firmware-88bbbdc6a77c4f0490a806ea90e659da/results.json |
| Diagnostico con VGA standard |10/10 consecutivi, GPU assente |virtio-initialization-firmware-a7206b88033f469694134741a60557f5/results.json |
| Console ordinaria con VirtIO |10/10 consecutivi, mouse/tastiera/cleanup |virtio-mapping-firmware-0d860f30b3cc4384a4fe897cd814b454/results.json |
| ACCESS_PLATFORM richiesto dal device |1 rifiuto intenzionale, prima delle code |virtio-initialization-firmware-4579b10a0bf145079a748208c411c34d/results.json |

Totale30 boot positivi finali; il negativo non e contato fra questi.
Nel diagnostico positivo QTest verifica indipendentemente status zero,
ramo GPU revocato, tutte le foglie della finestra e le guardie assenti.
La lettura fisica del blocco DMA conserva `dma-ram.bin`: descrittori,
indirizzi, indici avail/used256, otto completion finali e fence256 nella
richiesta e nella risposta sono verificati oltre ai marker del kernel.
La trace dispositivo conferma la sequenza finale0,1,3,11,15,0.
Budget fisico originale20456 pagine ripristinato nel diagnostico con GPU.

Il negativo usa `iommu_platform=on` per richiedere ACCESS_PLATFORM: il driver
non accetta la feature, il device rifiuta FEATURES_OK; FAILED/reset e
cleanup sono verificati prima del panic diagnostico previsto. Non dimostra
un IOMMU implementato. I fallimenti durante le code sono esercitati host,
non iniettati nel device QEMU in questa campagna.

La regressione ordinaria mantiene i tre lease delle tabelle GPU pinned
durante l'uso; il budget restituito riguarda le risorse temporanee delle
task, non ogni allocazione persistente del kernel.

## Review e limiti

Disassembly in `kernel/build/virtio-queues-disasm.txt`: MFENCE presente,
accessi MMIO scalari8/16/32bit. SSE2 verificata con CPUID prima dell'uso.
Trasporto senza nuova route IRQ; polling IF=0 e reset/ISR nel diagnostico.
Non e un'implementazione da riusare senza progetto con task/IRQ concorrenti.

Struttura/include/link/LF verificati dal runner di struttura VirtIO; nessun
nuovo sorgente supera400 righe o cinque file diretti per cartella funzionale.
Build principale386 righe. Diff controllato, `reuse/` e licenza invariati.

Un primo build ha rilevato un argomento PowerShell concatenato male; corretto
senza disabilitare warning. Corretto anche il percorso relativo del test
Rust prima dei test finali. Uno smoke preliminare positivo e separato in
`virtio-initialization-firmware-9efd0c3976764ea1ba455aace1d00630/`:
non viene contato fra i30 boot dell'immagine finale.

Non verificati: hardware fisico, DMA ostile/IOMMU, SMP, prestazioni e
presentazione video. Scanout, risorse2D e cursore VirtIO restano da fare;
la console ordinaria continua sul framebuffer firmware.
