# Intel VT-d: coerenza CPU e queued invalidation

21 ottobre 2026. Perimetro: invalidation/ e adattatori registri, inventario,
build e probe concordati. [Contratto approvato](design.md).

## Risultato e confine

C gestisce lease, pinning, MMIO, clock e cleanup; Rust puro valida geometria
cache, descrittori e accessi scalari. Il backend CPU controlla CPUID prima
di CLFLUSH/MFENCE. Nessun runtime Rust, allocator o parser hardware Rust.
Profilo BSP diagnostico, una unita Intel VT-d legacy e una sola richiesta
in volo, tre descrittori per richiesta: context globale, IOTLB globale,
wait con status e fence. Due pagine pinned, queue e completion separate.

Le tabelle private del dominio vengono verificate e pulite attraverso la
finestra WB posseduta. Nessuna scrittura a RTADDR o TE, nessun trasferimento
GPU, nessuna autorizzazione DMA attiva. Il completamento QI senza root
pubblicata prova il protocollo, non la revoca di traduzioni DMA utilizzate.

Le fonti e i bit sono nel design e nei backend: Intel VT-d rev5.0
D51397-017, Intel SDM093 di settembre2026; confronto Linux v6.18 e modello
QEMU v11.1.0. La copia locale VT-d ha SHA256
`A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6`.

## Ownership e failure

La nuova autorita Writable permette una sola unita RW, UC supervisor/NX
con guardie. Il vecchio bind conserva il modo RO. Lo snapshot registra e
ricontrolla il modo: inventario interno3840 byte, boot ABI v5 invariato.
Il consumer QI impedisce la revoca del mapping registri finche possiede
risorse. Pin e lease vengono ricontrollati prima degli accessi.

La tail e pubblicata soltanto dopo preparazione e flush dei descrittori;
un token vecchio e head=tail da soli non completano la richiesta. Deadline
PM di un secondo, limite4096 letture e limite256 campioni fermi: sono
limiti del diagnostico, non una latenza hardware universale. Nessun IRQ
di completion, PASID, ATS o command queue concorrente.

Enable, completion o disable incerti mantengono RAM e mapping. Lo stop
richiede ultimo wait completato, head=tail e QIES spento; soltanto dopo
scrive IQT/IQA zero e rilascia. Nessun clear W1C generico degli errori.
I retry host provano conferme tardive; un errore non diventa PASS.

QEMU conserva nel readback IQA l'indirizzo latched dopo disable. Il test
accetta esclusivamente zero o l'esatta pagina precedentemente posseduta,
con QIES=0; la traccia verifica le scritture finali zero. Non dichiariamo
che il readback IQA emulato sia zero. Vedere il dettaglio nel design.

## Test host eseguiti

Runner: `../scripts/test.ps1`; evidenze sotto kernel/build/:

| Prova | Risultato | Evidenza |
| --- | --- | --- |
| C QI/ASan, allocatore reale | 176349 controlli | iommu-qi-host.log |
| Rust policy debug e release | 3 test per configurazione | iommu-qi-host.log |
| Clippy, warning come errori | PASS | iommu-qi-host.log |
| CPU nativa CPUID/CLFLUSH/MFENCE | PASS, line64 | iommu-qi-host.log |
| Cppcheck2.22 esaustivo | 0 bloccanti,18 style,17 information | iommu-qi-host/cppcheck.xml |
| Mapper registri RO | 95776 lifecycle +696320 accessor | register-host-qi-regression.log |
| Dominio privato | 925362 controlli | domain-host-qi-regression.log |
| Paging | 8 gruppi x100 | paging-host-qi-regression.log |
| Mapper VirtIO | 793400 controlli | virtio-host-qi-regression.log |
| Framebuffer | lifecycle, pixel e drawing PASS | framebuffer-host-qi-regression.log |
| Risorse/scanout host | 8200+19900+6300 controlli | resources-host-qi-regression.log |
| Kernel host | suite completa PASS | kernel-host-qi-regression.log |

Il loop principale QI comprende2560 completion simulate, wrap multipli,
entrambi i modelli IQA (normale e latched), token errati, clock fermo,
timeout enable/completion/disable, errori, conservazione dei pin e retry.
Ulteriori casi verificano esaurimento lease, errore di ciascun pin,
binding occupato, metadati Writable corrotti, feature mancanti, vecchio
IQA allineato/invalido, borrower con opaque obbligatorio e pulizia del
dominio. Rust percorre anche offset e larghezze della whitelist; TE,
RTADDR e scritture fuori registro vengono rifiutati.

ASan strumenta il C host; CPU/MMU/MMIO e clock del test di lifecycle sono
mock. Il test CPU separato esegue davvero CLFLUSH/MFENCE sulla RAM host e
verifica limiti e contenuto; non misura visibilita di un IOMMU fisico.
Le note cppcheck includono header di sistema assenti e readback volatile
considerati costanti dal modello statico: il report non e privo di note.
Valgrind non disponibile; nessun risultato Valgrind o percentuale coverage
inventati. Il budget fisico viene controllato separatamente dal sanitizer.

## Debug prima della campagna finale

Corretti due rifiuti del probe preliminare: il borrower reale richiede
opaque non NULL (il vecchio mock lo permetteva), e QEMU legge l'IQA latched
soltanto dopo enable. Il test host ora riproduce entrambi i contratti.
Il controllo IQA e stato spostato dopo QIES; ownership e stop non sono
stati rimossi. Gli smoke precedenti non contano nelle campagne finali.

## QEMU sull'immagine finale

QEMU11.1.0 build v11.1.0-12130-ge470268ff4, Q35/TCG, una CPU,128MiB,
OVMF fornito con QEMU. Campagne sequenziali, VM possedute, deadline finite
e hash delle copie ESP verificati. Tutti i positivi raggiungono il marker
richiesto senza fault inattesi; i negativi richiedono il rifiuto specifico.

| Profilo | Boot | Directory sotto kernel/build/ |
| --- | --- | --- |
| QI con VT-d e GPU | 10 | iommu-discovery-firmware-9ef9b778be0e405f9e39a11f3a67bd99 |
| QI senza GPU | 10 | iommu-discovery-firmware-ae658e814641426f8a3df0079481e9af |
| QI senza IOMMU | 10 | iommu-discovery-firmware-b56c4fa7e54b491d83bb8aebe3f42d1e |
| Mapper RO, regressione | 10 | iommu-discovery-firmware-acd910d4e0144161b4989bfe1ca51987 |
| Dominio privato, regressione | 10 | iommu-discovery-firmware-2c03849326a74383a31249995335415a |
| Console ordinaria, input/cleanup | 10 | virtio-mapping-firmware-92dc8a53be6c4fef83e252f93c5e0053 |
| Scanout, due screenshot per boot | 10 | virtio-initialization-firmware-875aa83364a64f708cb95d481884483b |
| CPUID senza CLFLUSH, rifiuto atteso | 1 | iommu-discovery-firmware-13499327f6dc4082a1e5cd3b40f8144c |
| DRHD su RAM, rifiuto atteso | 1 | iommu-discovery-firmware-922329bf8e9a48ab9e20cc4369f10c4f |

Totale70 positivi, dieci consecutivi per profilo, piu due negativi attesi.
Il negativo DRHD viene respinto prima di QI; non e una prova di fault DMA.
Il negativo CPUID verifica nessuna scrittura QI e budget ripristinato.
Le assenze non abilitano la coda. Gli smoke non entrano nel conteggio.

Nel positivo QI ogni boot completa128 batch e384 descrittori:1280 wait e
3840 descrittori complessivi nel motore emulato. QTest ricostruisce tutti
i4096 byte del ring finale dopo wrap e la pagina completion, token128 e
tail2048. Verifica inoltre GSTS/RTADDR/FSTS/head/tail zero, IQA latched
esattamente posseduto, PML4[228] revocato e tutte le entry delle tre tabelle
CPU ritirate (RW/NX, UC e guardie). Lo scanner del kernel verifica le PTE
durante l'uso: lo snapshot esterno e delle tabelle dopo revoca, non di
tabelle live. Le pagine liberate sono lette prima del riuso nel probe.
La traccia verifica context/IOTLB/wait in ordine, enable/disable e whitelist
delle scritture. La pulizia delle14 tabelle private viene completata prima
di QI. Accounting fisico prima/dopo uguale; nessuna RAM liberata per
nascondere un timeout. La regressione scanout confronta20 screenshot interi.

## Immagini, riproduzione e review finale

SHA256 delle immagini sotto kernel/build/:

| Immagine | SHA256 |
| --- | --- |
| iommu-qi-final/kernel.elf | 31016AAC57E4BF7C2462123FA1F6859291E1DAE239053A585FB13BC1A27995A7 |
| iommu-qi-ro/kernel.elf | 3212CB8EDDA74D2A60E89151EF2559EBAFA6638A3C90CB337AB517018031D66C |
| iommu-qi-domain/kernel.elf | 651DC3FD0B462E1A03853543A303A15C7A204A3BA59D5CD5BBFC2747B87A334F |
| iommu-qi-ordinary/kernel.elf | D1C5C22BA809BF08CCDA9E0172E3FD9BFE3D6926D547182D8A269F9809D3342F |
| iommu-qi-vgpu/kernel.elf | C3D5703D71CB54D16726D5D301D447A11BE1EEBCA52A4CDC99CCA735B61F5469 |

Loader invariato:
`C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.
Il MAP ordinario esclude entrypoint QI, dominio e probe registri; nessuna
installazione nella ESP ordinaria, nessun push o modifica a reuse/.

Build: `powershell.exe -NoProfile -File kernel/scripts/build.ps1 -TestIommuQi -OutputDirectory D:/OS/kernel/build/iommu-qi-final`.
Host: `powershell.exe -NoProfile -File arch/x86/kernel/drivers/memory/iommu/intel/invalidation/scripts/test.ps1`.
QEMU: Python3 con `-B arch/x86/kernel/drivers/memory/iommu/intel/discovery/scripts/firmware.py --qi --image kernel/build/iommu-qi-final/kernel.elf --boots 10`.
Per le assenze aggiungere `--gpu-absent` oppure `--absent`; per il negativo
CPU usare `--qi-no-clflush` e `--boots 1`.

Manifest sorgenti/adattatori: `kernel/build/iommu-qi-structure.json`.
Massimo398 righe, cinque sorgenti diretti nel modulo. Include, link,
fine riga LF, whitespace e diff del perimetro verificati. Le build native
e i test Rust trattano i warning come errori. Nessuna percentuale coverage
misurata. Risultati finali aggregati in `kernel/build/iommu-qi-final-evidence.json`.

## Limiti e prossimo gate

Queste prove non coprono hardware fisico, AMD-Vi, SMP, DMA ostile, fault
di traduzione o revoca dopo DMA reale. Non viene estesa l'eccezione IOMMU
della console. Servono ancora routing DRHD/device, pubblicazione della
root, attivazione, fault handling e prove DMA consentito/vietato, quindi
ACCESS_PLATFORM e integrazione VirtIO persistente. Una pulizia cache non
e una ricevuta riutilizzabile dopo modifiche alle tabelle.
