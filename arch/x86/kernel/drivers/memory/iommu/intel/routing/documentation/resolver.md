# Resolver GPU–DRHD: profilo implementato

22 ottobre 2026. Specifica esecutiva della parte resolver del
[contratto B approvato](expanded-design.md). Topologia, DMAR e identita
GPU sono input immutabili; output privato e disgiunto, leggibile dal
chiamante dopo il ritorno. Nessun nuovo MMIO, DMA o Assembly.

## Fonti verificate

[Intel VT-d 5.0 D51397-017](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf),
8.3/8.3.1, pp8-3..8-6: flags DRHD bit0 INCLUDE_PCI_ALL, segmento e
scope; endpoint/type1, sub-hierarchy/type2, IOAPIC/type3 e HPET/type4.
Il path parte dal root bus e segue i secondary bus dei bridge. Uno scope
di gerarchia comprende il bridge identificato e i suoi discendenti.
Include-all viene dopo gli scope espliciti dello stesso segmento.
PDF locale in kernel/build/iommu-research/vtd-831418.pdf, SHA256
A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6.

[Linux v6.18 dmar.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/iommu/intel/dmar.c)
e il confronto di metodo per segmenti, device/antenati e fallback finale;
non vengono copiate implementazione, workaround firmware o licenza.
Il nostro profilo rifiuta gli input incerti invece di tentare riparazioni.

Il verifier host legge soltanto GSTS0x1c/TES31 e RTADDR0x20,
Intel11.4.4.2/11.4.5. Controlla inoltre una trace contenente esclusivamente
l'evento QEMU v11.1.0 vtd_reg_write: deve restare vuota. Non abilita TE.

## ABI e validazione

Input DMAR992 byte, nodo PCI32 byte e request20 byte, con static assert
C/Rust. Request: Segment/Bus/Device/Function/Id. Richiede un endpoint GPU
di classe display presente nello snapshot con identita uguale.
Output40 byte: Base/Bytes, Unit/Node/Scope/Kind, Segment/Requester, Reserved0.
Requester e il BDF software a16 bit, non una prova sugli alias DMA.

Massimo8 unita,32 scope,512 nodi e8 coppie di path. Topologia con
profondita8 puo richiedere9 coppie da root: tale path esplicito non viene
inventato o troncato; include-all rimane possibile se gli altri input
sono validi. Per questa versione nessun namespace/type5, record DMAR non
interpretato o supporto implicito ARI/SR-IOV/ATS/PASID/requester alias.

Si verificano counts, reserved, code inutilizzate azzerate, dimensioni
e geometria dei registri, ownership contigua degli scope, include-all
unico e ultimo nel segmento. Le unita non possono sovrapporsi fisicamente.
Questi controlli strutturali non autorizzano il mapping dei registri:
il futuro owner deve ancora escludere RAM/alias e verificare le capacita.

Per gli scope PCI del segmento GPU si risolve ogni passaggio, anche se
lo scope non sembra interessare la GPU. Il primo nodo e radice; ciascun
successivo ha esattamente il bridge precedente come parent. Non si
assume bus0. Terminale endpoint/header0 o bridge/header1 secondo il tipo.
Scope irrisolvibile impedisce il fallback. Gli scope PCI di altri segmenti
non vengono risolti usando arbitrariamente il segmento GPU.

Nel profilo v0 gli scope PCI espliciti devono essere disgiunti: duplicati
o gerarchie sovrapposte, anche nella stessa unita, vengono rifiutati.
Si rifiutano inoltre IOAPIC/HPET duplicati per tipo/enumeration nel
segmento interessato; non sono mai match GPU. Questo non implementa
la gestione interrupt remapping o la correlazione completa MADT/HPET.

Sequenza non eseguibile:

    INPUTS_VALID -> PATHS_RESOLVED -> CLAIMS_DISJOINT
        -> EXPLICIT_MATCH | UNIQUE_SEGMENT_FALLBACK | NO_MATCH
        -> PRIVATE_RESULT -> RETURN

Rust puro senza heap controlla strutture, grafo, path e precedenze.
Solo FFI converte puntatori C in riferimenti/slice dopo null, alignment,
count e overflow; capacita, mapping e durata sono obblighi del chiamante.
C verifica disgiunzione dell'output e topologia, interpreta la selezione
e copia40 byte solo in caso FOUND. La copia non e una pubblicazione
atomica per lettori concorrenti. Nessuna ownership viene trasferita.

ABI FFI interna:0 rifiuto,1 nessun match; per un match bits7:0 unit+2,
bits15:8 kind1/2/3, bits23:16 scope0..31 o32 per fallback, bits40:32 node.
Altri bit zero. API C:0 INVALID,1 ABSENT,2 FOUND; negli altri casi output
precedente invariato. Scope nel risultato pubblico e0xffffffff per fallback.

## Minacce e confini

Selezione errata da firmware ambiguo: rischio HIGH nel futuro percorso
DMA; mitigazione rifiuto, precedenze esplicite e confronti indipendenti.
OOB/overflow da counts o path: limiti fissi, accessi Rust checked, FFI
ristretta, ASan e test negativi. Corruzione dell'input tramite output:
disgiunzione e pubblicazione finale. Input condivisi mutabili, puntatori
user non mappati e sincronizzazione SMP non fanno parte di questa API.

La selezione non protegge da DMA ostile e non costituisce una capability
riutilizzabile dopo hotplug/ABA. Il modulo non avvia DMA; non estende
l'eccezione IOMMU dei precedenti diagnostici alla console ordinaria.

## Prove previste

Host: tutti i BDF, mutazioni DMAR, alias, output invariato, massimi,
segmenti, scope sovrapposti, ordering, path8 e rigetto namespace/record.
QEMU: DMAR reale e confronto binario con parser/resolver indipendente,
GPU radice/annidata, assenza DMAR ricontrollata dalla XSDT e budget.
Quattro fixture sintetiche nel guest verificano otto unita, scope
specifico contro fallback, gerarchia e conflitto: non sono otto IOMMU
hardware emulate. Regressioni topologia, QI e console, dieci boot per
profilo finale. Risultati reali nel report di validazione del blocco.
