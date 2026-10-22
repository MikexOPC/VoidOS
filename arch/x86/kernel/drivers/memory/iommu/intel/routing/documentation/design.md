# Associazione GPU–DRHD: ricerca iniziale e alternativa A non adottata

22 ottobre 2026. L'utente ha scelto B: topologia PCI con bridge e piu
unita subito. Il contratto A sotto riportato non e quello da implementare.
Il [progetto esteso B](expanded-design.md) e il riferimento corrente.
Nessun codice eseguibile implementato da questi documenti. Il blocco precedente e
[coerenza CPU e queued invalidation](../../invalidation/documentation/validation.md).

## Obiettivo e perimetro

Dimostrare quale unita VT-d descritta dal firmware gestisce il requester
PCI della GPU, prima della pubblicazione della root. Modulo routing/;
soli adattatori discovery diagnostica, build e probe necessari. Non cambia
il contratto boot, non legge configurazione PCI attraverso nuove porte,
non modifica registri, scheduler o allocatore. reuse/ resta intatto.

L'identita GPU proviene dallo snapshot PCI del contratto boot corrente;
DMAR e gia decodificata dalla discovery. Il risultato e un candidato
verificato per questi snapshot, non un'autorizzazione permanente a DMA.
Il futuro owner di attivazione dovra rivalidare identita e associazione.

## Ricerca verificata

[Intel VT-d rev5.0 D51397-017](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf),
§8.3, pagina8-3, e §8.3.1, pagine8-4..8-6. Copia locale verificata con
SHA256 `A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6`;
estratto in kernel/build/iommu-research/routing-spec.txt.

- DRHD flags, byte4 bit0 INCLUDE_PCI_ALL: gli scope espliciti delle altre
  unita nello stesso segmento hanno precedenza. Una sola unita presente
  non implica che gestisca ogni dispositivo.
- Segment e un campo16bit al byte6. Lo scope contiene start bus al byte5
  e coppie device/function dal byte6. Un path con piu coppie richiede
  risolvere i bus secondari dei bridge: l'ultima coppia non e un BDF completo.
- Scope type1 descrive un endpoint; type2 include il bridge e i dispositivi
  downstream. IOAPIC/type3 e HPET/type4 non provano ownership della GPU.

[Linux v6.18 dmar.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/iommu/intel/dmar.c),
dmar_register_drhd_unit, dmar_pci_device_match e
dmar_find_matched_drhd_unit: separa scope risolti, gerarchia PCI e fallback
include-all. Questa e una distinzione di responsabilita, non codice da
copiare. Il progetto non ha ancora un gestore generico della topologia PCI.

[QEMU v11.1.0 intel_iommu.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/i386/intel_iommu.c)
e il modello emulato, non una garanzia hardware fisica. Il suo percorso
vtd_do_iommu_translate gestisce il requester; il nostro modulo seleziona
il DRHD dalla descrizione firmware prima dell'attivazione.

Nel campione precedente, non in un nuovo boot del22 ottobre:
kernel/build/iommu-discovery-firmware-9ef9b778be0e405f9e39a11f3a67bd99/boot-1/dmar.bin
contiene un DRHD, segmento0, flags0, sei scope: un IOAPIC e cinque
endpoint diretti sul bus0, fra cui01.0. Questo identifica la forma da
collaudare; non autorizza a codificare01.0 come indirizzo fisso della GPU.

## Alternative da discutere

A, proposta: una GPU sul bus radice0 del segmento0 e una sola unita VT-d.
Scope endpoint diretti oppure INCLUDE_PCI_ALL con semantica verificata.
Rifiutare routing non dimostrabile. Si riusa il perimetro del mapper RW
gia concordato, che consente una sola unita. DMA e root restano inattivi.

B: risoluzione generica della topologia PCI, multihop, sub-hierarchy,
piu segmenti/unita e associazioni dinamiche. Richiede un contratto PCI
aggiuntivo, accessi configurazione, lifetime e test di topologie distinte.
Non accelera la prova iniziale sul profilo QEMU attuale; resta un upgrade.

## Contratto proposto per A

Ingresso bounded: inventario DMAR, identita GPU e limiti degli snapshot.
Rivalidare numeri, riservati, intervalli FirstScope/ScopeCount, appartenenza
Unit, profondita, device<=31 e function<=7, segment/bus e assenza di record
DMAR non interpretati. OtherRecords diverso da zero e un rifiuto: RMRR,
ATSR o altri requisiti non diventano autorizzati perche saltati dal parser.

Una sola unita, segmento0, GPU presente sul bus0. Nel segmento interessato
scope type1 con Depth diverso da1 e scope type2 vengono rifiutati anche
se sembrano estranei alla GPU: non possiamo risolverne l'effetto sulla
selezione senza topologia. IOAPIC/HPET sono validati ma non usati per il
match GPU. Altri tipi non supportati vengono rifiutati.

Senza include-all deve esistere un solo match endpoint esatto di bus,
device e function. Match duplicati o inventario incoerente sono errori.
Con include-all sono vietati scope PCI type1/2 come da specifica; il
segmento corretto costituisce il match, senza ignorare scope non risolti.
GPU/IOMMU assente e distinto da descrizione invalida o non supportata.

C attraversa gli array bounded e pubblica soltanto alla fine un risultato
disgiunto dagli input. Rust puro valida i campi scalari, il confronto del
requester e la scelta del tipo di scope, senza allocazioni o unsafe.
Nessun lease o puntatore a RAM firmware conservato nel risultato.
Output invariato su assenza/errore; nessun effetto MMIO o DMA.

Risultato candidato: indice e base/span/segmento DRHD, identita requester,
motivo del match (endpoint o include-all). Non rappresenta un dominio
attivo, una protezione contro spoofing PCI o una concessione di DMA.

## Sequenza non eseguibile

    INPUT -> VALIDATE_SNAPSHOTS -> CHECK_SUPPORTED_TOPOLOGY
          -> MATCH_REQUESTER -> UNIQUE_CANDIDATE -> PUBLISH_OUTPUT

    assenza -> ABSENT, output invariato
    incoerenza -> INVALID, output invariato
    topologia/record non supportati -> UNSUPPORTED, output invariato

Nessuna transizione scrive RTADDR, GCMD o avvia richieste VirtIO.
Per ogni chiamata il candidato viene ricostruito; nessun caching di una
decisione di sicurezza attraverso cambi di identita o inventario.

## Criteri di accettazione dopo la scelta

1. C/Rust: match diretto, include-all, GPU/IOMMU assenti, segmento/BDF
   errati, duplicati, path multihop, type2, record non interpretati,
   riservati, limiti, alias input/output e output immutato su rifiuto.
2. ASan host e allocazioni del modulo nulle; nessun risultato Valgrind o
   coverage inventato. Rust debug/release, Clippy e warning come errori.
3. QEMU: dieci boot consecutivi con confronto indipendente snapshot
   PCI/DMAR e risultato guest; assenze e negativi distinti dai positivi.
   Root non pubblicata, DMA disattivato, nessuna nuova scrittura hardware.
4. Regressione QI e console ordinaria; ownership/budget, build, include,
   dimensioni e diff finale verificati. Test centralizzati in percorso
   speculare, script nel modulo, artefatti in build/.

## Blocchi successivi, non implementati qui

Pubblicazione/attivazione richiede uno stato posseduto distinto e gestione
fault prima di permettere traffico DMA. TE agisce sull'unita, non soltanto
sulla GPU: prima dell'enable va definita la sorte degli altri requester,
senza passthrough implicito o interruzione non valutata dei device attivi.

Occorre poi provare accessi DMA consentiti/vietati, fault osservati,
invalidazione e revoca prima del riuso della RAM. La console VirtIO
persistente e ACCESS_PLATFORM seguono quelle prove. Questo documento
non anticipa l'approvazione di tali contratti hardware.
