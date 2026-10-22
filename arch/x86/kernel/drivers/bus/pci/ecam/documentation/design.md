# Configurazione PCI nativa: MCFG/ECAM

22 ottobre2026. Contratto scelto: lettore nativo kernel, mapping temporaneo
posseduto, bounds e rollback. Prerequisito del [routing con bridge e piu
unita](../../../../memory/iommu/intel/routing/documentation/expanded-design.md).
Questo blocco non enumera l'intera topologia e non abilita DMA.

## Fonti verificate prima degli accessi

[Intel3Series316966-002](https://www.intel.com/Assets/PDF/datasheet/316966.pdf),
§4.3.2, pagina78: indirizzo ECAM = base + bus*1MiB + device*32KiB +
function*4KiB + offset. Le finestre MCFG non cambiano il significato del
numero di bus: non sottrarre start_bus alla formula con base firmware.
Tabelle5-1/6-1: letture DWORD allineate di ID0, class8, header0x0c e0x18
(bus per header bridge, BAR per endpoint). Nessun config write, probe BAR,
renumbering, lock transaction o lettura device-specific.

[ACPICA/Intel actbl2.h, Linux v6.18](https://raw.githubusercontent.com/torvalds/linux/v6.18/include/acpi/actbl2.h),
MCFG version1: header ACPI36 +8 riservati; allocation16, base/u64 a0,
segment/u16 a8, first/last a10/11, reserved/u32 a12. Revisione e checksum
sono controllati. [Linux v6.18 pci_mcfg.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/acpi/pci_mcfg.c)
e [ecam.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/pci/ecam.c)
sono confronti per separare parser, finestra e device, senza copiarne codice.
[PCI-SIG ECN](https://pcisig.com/PCIFirmware/ECN/Firmware/EnablingMultipleBaseAddressesperPCISegmentGroup)
motiva piu finestre non sovrapposte nello stesso segmento.

[QEMU v11.1.0 q35.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/pci-host/q35.c)
e il backend chipset del collaudo. Il sorgente e la specifica non
certificano altri chipset o firmware: eventuali quirks richiedono un
backend esplicito, non un indirizzo aggiustato per far passare i test.

## Formati, limiti e rifiuti

MCFG massimo556 byte:44 +32*16. Massimo32 finestre e8 segmenti distinti.
Info528 byte con sole copie dei dati, nessun puntatore firmware. Piu
finestre nello stesso segmento sono ammesse con bus disgiunti. Rifiutare
overlap fisico anche fra segmenti diversi, overlap bus nello stesso
segmento, overflow, base non allineata a1MiB, first>last, riservati o
entry inutilizzate nonzero. Capacita superate non vengono troncate.

Il finder usa la directory ACPI gia verificata e la finestra di lettura
ACPI. Una sola MCFG, header ricontrollato contro la lettura intera,
nessun output parziale. Assenza=2, invalidita=0, presente=1. Output
immutato su assenza/errore; scratch e output devono essere disgiunti.

## Ownership e cache

Owner C non spostabile, zero iniziale, con copia Info, root/PAT/budget e
riferimenti a contesto, inventario e page window. Il profilo CPU esistente
richiede BSP, IF=0 e paging atteso prima di mutare le PTE. Non si dichiara
sincronizzazione SMP tramite IF=0. La pagina ECAM non e RAM allocata.

Prima dell'apertura si verificano le estensioni fisiche delle finestre:
nessuna RAM firmware/kernel o disponibile, lease live, framebuffer,
pagina tabella o alias ordinario. Regioni EFI incontrate devono essere
reserved/MMIO e compatibili con UC. La descrizione MCFG autorizza gli
intervalli non presenti nella memory map, dopo le esclusioni; non viene
presentata come interpretazione di ACPI _CRS/_CBA, ancora assente.

Per ogni lettura: owner e Info validi, CPU invariata, inventario completo,
page window idle, indirizzo dentro una sola finestra, pagina ancora
sicura. Un solo prestito alla volta, UC supervisor RO/NX, guardie della
page window esistente. Nessuna tabella CPU aggiunta, allocazione fisica o
autorita persistente nell'inventario. Il borrower preesistente verifica
PTE prima/dopo il callback, revoca e invalida, poi verifica stato idle.

Il valore viene scritto al chiamante solo dopo cleanup e verifica finale.
L'output non puo sovrapporsi a owner, contesto, pool, snapshot paging,
memory map, LAPIC o aperture protette. Le uniche letture ammesse sono
DWORD agli offset0/8/12/24: la futura topologia estendera la policy solo
se servono registri ulteriori con fonte e test, non una memcpy di4KiB.

## Stati e recovery

Sequenza non eseguibile:

    EMPTY -> OPEN_IDLE -> BORROWED -> OPEN_IDLE -> CLOSED
                           |
                      REVOKE_UNCERTAIN

Un errore callback con revoca riuscita conserva la sessione, senza valore
pubblicato. Una revoca incerta rende l'owner inutilizzabile per nuove
letture. Close richiede page window idle, CPU/inventario e budget validi;
solo allora azzera i metadati. Non libera la finestra o risorse del pool
per nascondere l'errore. Le risorse della page window restano del framework
preesistente. La sessione diagnostica richiede budget invariato.

Rust puro valida indirizzi, geometria e whitelist, senza unsafe o heap;
C gestisce owner e borrower. ABI FFI esclusivamente scalare. La gestione
snapshot stabile della topologia e un contratto successivo: singole
letture valide non provano che un bridge non sia cambiato tra due accessi.

## Accettazione

Test C/ASan e Rust debug/release su finestre, estremi, bus iniziale nonzero,
segmenti, alias e output immutato; pin/lease accounting del pool reale;
callback fallito, revoca incerta, budget cambiato e retry verificati.
Cppcheck e warning come errori. Dieci boot QEMU consecutivi per profilo
positivo, confronto esterno MCFG/ECAM e CF8 nel segmento0, GPU su bus0 e
dietro root port, assenza MCFG e negativo duplicato; regressioni QI e
console ordinaria. Il ramo assente puo essere collaudato su Q35 con un
filtro dichiarato nel solo adattatore diagnostico, senza presentarlo come
assenza fisica della tabella. Prove e limiti nel report validation.md del blocco.

Il loader e il contratto boot v5 restano invariati. Segmenti nonzero
sono collaudabili nei test host; il percorso GPU corrente del loader li
rifiuta ancora e non viene dichiarato esteso da questo modulo. Niente
hotplug, _CBA, quirk non-ECAM, ARI, selezione DRHD o traduzione DMA attiva.
