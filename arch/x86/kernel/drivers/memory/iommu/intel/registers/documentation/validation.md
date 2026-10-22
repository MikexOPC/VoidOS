# VT-d: mapping posseduto e registri, validazione

20 ottobre 2026. [Design e fonti](design.md). Scelta utente: mapping
permanente gia in questo blocco, con adattatori paging, build e test.
Questo checkpoint riguarda il mapping CPU dei registri; non attiva DMA.

## Risultato

Owner non spostabile, fino a otto unita: prima pagina registri UC,
supervisor RO/NX, guardie, tre tabelle pinned nel ramo PML4[228].
Pubblicazione dopo costruzione e readback; revoca e flush precedono il
rilascio. Errori di root, flush o capture trattengono le risorse per retry.
RAM, lease e alias correnti non sono adottati come registri.
L'inventario paging passa da 3720 a 3832 byte; ABI boot v5 invariata.

C gestisce ownership, mapping e letture volatile di larghezza naturale;
Rust puro valida PTE e campi delle capacita, senza unsafe o allocazioni.
Il bind avviene nel bootstrap, con LAPIC opzionale, prima del video.
Il diagnostico esercita due bind/read/unbind; permanente significa che
il mapping resta valido fino a revoca esplicita, non che viene lasciato
attivo al termine di questa prova. La console ordinaria esclude mapper,
lettore e probe VT-d; il registro paging condiviso resta presente e vuoto.

## Host e review

Runner: `arch/x86/kernel/drivers/memory/iommu/intel/registers/scripts/test.ps1`.
Log `kernel/build/iommu-registers-host.log`; XML e binari in
`kernel/build/iommu-registers-host/`.

| Prova | Risultato |
| --- | --- |
| Mapping C/ASan | 95.776 controlli, 800 cicli con allocatore fisico reale e da 1 a 8 unita |
| Accessor indirizzi | 696.320 controlli su bounds, larghezze e whitelist; nessuna dereferenziazione MMIO in questo test |
| Rust | 2 test debug e 2 release, Clippy e warning come errori |
| Compilatore C | MSVC C11 /W4 /WX; ASan host senza segnalazioni |
| cppcheck 2.22.0 | 0 error/warning/performance/portability; 10 style e 13 information conservati nell'XML |
| Isolamento | `-TestIommuRegisters -Install` rifiutato prima della build |

Casi: allineamenti, guardie e alias, output sovrapposto all'owner, lease
non pinned, foglia resa scrivibile, guardia occupata, output immutato su
capacita invalida, stato TES gia attivo non autorizzato, root estranea,
flush/borrow/capture/CPU falliti e retry. L'esaurimento dei lease durante
la terza allocazione verifica il rollback delle prime due tabelle.
CPU, MMU e MMIO sono mock nel test C; il registro authority e l'allocatore
sono reali. Il mock capture non sostituisce lo scanner paging di produzione.

Regressioni finali, log `kernel/build/iommu-registers-regression-N.log`:

- 0: paging, 8 gruppi ripetuti 100 volte, cleanup dei lease pinned.
- 1: mapping GPU, 793.400 controlli, 100 cicli con allocatore reale.
- 2: framebuffer, 7 gruppi lifecycle x100, 3 pixel e 5 drawing x100.
- 3: risorse VirtIO, 6.300 controlli mapper, 100 cicli; suite protocollo/Rust.
- 4: suite kernel host, completata senza errori; conteggi nei singoli test.

Valgrind non eseguito: non disponibile per questo profilo Windows.
ASan copre gli accessi host strumentati; accounting e budget misurano
separatamente le pagine fisiche. Nessuna dichiarazione di leak universali
assenti, coverage linee/branch N/D.

## QEMU e prove indipendenti

QEMU 11.1.0 build v11.1.0-12130-ge470268ff4, Q35/TCG, una CPU, 128 MiB,
OVMF fornito con QEMU. VM possedute, deadline finite, hash ESP verificati.
Runner discovery `scripts/firmware.py --registers`, dieci boot per campagna:

| Profilo | Boot | Evidenze sotto kernel/build/ |
| --- | --- | --- |
| Intel VT-d presente | 10 | iommu-discovery-firmware-b83ef42384cf4feeb94e302c1035495e |
| IOMMU assente | 10 | iommu-discovery-firmware-f6d9489edd044a55869ffa8844f1d041 |
| Console ordinaria, input e cleanup | 10 | virtio-mapping-firmware-6e15e96bca574e10be32203655968b34 |
| Regressione scanout VirtIO, due immagini per boot | 10 | virtio-initialization-firmware-b15c5b159a094087b541fef071838bb6 |
| DMAR duplicata, rifiuto atteso | 1 | iommu-discovery-firmware-ede1b2997fcb45e194eae01ec5aab097 |
| DRHD che indica RAM, rifiuto atteso | 1 | iommu-discovery-firmware-80f83d6401c841b0ace72540580370e6 |

Totale: 40 boot positivi, dieci consecutivi per profilo, e due negativi.
La regressione scanout confronta integralmente 20 screenshot a 1280x800,
conferma reset e revoca del mapping; resta nel diagnostico QEMU fidato.
Nessun fault inatteso nei profili positivi. I due negativi richiedono il
panic specifico e budget invariato: non sono boot positivi.
Lo smoke preliminare non e incluso nei conteggi finali.

QTest confronta VER/CAP/ECAP/GSTS con entrambe le letture guest dopo
rebind. Dopo revoca verifica PML4[228] vuoto e tutte le entry delle tre
tabelle ritirate: indirizzi, RO/NX, UC e guardie. Non e uno snapshot
indipendente delle PTE mentre il mapping e attivo: in quella fase lo
scanner reale del kernel verifica l'inventario prima/dopo le letture.

Valori osservati: VER=0x10, CAP=0x80D2008C222F0686, ECAP=0xF42,
GSTS=0; MGAW=48, ND=65.536 domini, IRO=0xF0, FRO=0x220, un fault record.
ECAP.C=0: `Compatible=0` per il candidato coerente ristretto. Il prossimo
progetto deve affrontare la coerenza delle strutture di traduzione;
nessun bit viene ignorato per forzare l'attivazione. Nessuna scrittura ai
registri del dispositivo, nessun dominio DMA o IOTLB attivato.

## Immagini e tracciabilita

SHA-256:

- Diagnostico: `87A12D33C06469E29FEFEDCBDE1F68E1E85B50087CFB4A6B351D909141AB1BB1`.
- Console ordinaria: `0AE212F60B189F3DD56FA5FC0B314E5F5AF66019A832859C1223A94A7CCF96C7`.
- Regressione scanout: `3B91329D5CC388ED8B942DFACDCD1C67998360D3CA8D8702053EC6E894C3C87C`.
- Loader invariato: `C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.
- Fonte Intel VT-d rev5.0 D51397-017: `A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6`.

Manifest sorgenti/adattatori: `kernel/build/iommu-registers-structure.json`.
Massimo 396 righe nei file controllati, cinque sorgenti diretti nel modulo.
Review finale: ownership, cleanup, fonti e diff circoscritto verificati.
Nessuna installazione ESP o modifica a reuse/. Prove su hardware fisico,
SMP, fault DMA e revoca IOTLB non eseguite; restano gate successivi.
