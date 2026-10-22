# Routing GPU–VT-d con bridge e piu unita

22 ottobre 2026. Scelta utente B accettata: topologia PCI con bridge e
piu unita subito. Sostituisce il contratto A della [ricerca iniziale](design.md).
Scelta successiva ricevuta: lettore nativo kernel MCFG/ECAM (N).
Il [modulo di accesso ECAM](../../../../../bus/pci/ecam/documentation/validation.md)
e verificato. Anche lo [snapshot topologico](../../../../../bus/pci/topology/documentation/validation.md)
e implementato e collaudato: trenta boot topologici e trenta di regressione,
con grafo QMP e registri confrontati indipendentemente. Il resolver descritto
qui e implementato e [validato](validation.md) il22 ottobre2026; lo snapshot non certifica
selezione DRHD, requester alias o isolamento DMA.

## Risultato richiesto

Associare il requester GPU al DRHD corretto risolvendo tutti i passaggi
PCI necessari. Non assumere bus0, unit0 o un BDF GPU fisso. Supportare gli
scope endpoint multihop, gli scope sub-hierarchy e include-all, con
separazione dei segmenti. Le unita DMAR restano al massimo8 e gli scope32,
come nel contratto discovery corrente; path massimo8 coppie.

La selezione logica non abilita tutte le unita: il mapper RW/QI precedente
rimane limitato a una unita posseduta. L'adattamento per selezionare e
attivare quella scelta, e l'eventuale gestione concorrente di altre unita,
richiedono il successivo contratto di attivazione. Nessuna scrittura a
RTADDR, TE o device durante il blocco di topologia e routing.

## Fonti e verifiche del repository

[Intel VT-d5.0 D51397-017](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf),
§8.3 e §8.3.1, pagine8-3..8-6: segmento, scope, path, bridge e priorita
degli scope espliciti rispetto a INCLUDE_PCI_ALL. Copia locale gia
verificata, SHA256
`A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6`.

[Linux v6.18 dmar.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/iommu/intel/dmar.c),
dmar_pci_device_match e dmar_find_matched_drhd_unit: confronto con device e
antenati, poi include-all nello stesso segmento. Nessuna copia del codice.

[PCI-SIG, ECN PCI Firmware3.2](https://pcisig.com/PCIFirmware/ECN/Firmware/EnablingMultipleBaseAddressesperPCISegmentGroup)
documenta piu intervalli MCFG nello stesso segmento. Non progettare una
tabella indicizzata solo per segmento con un unico base address implicito.
[Documentazione Linux ACPI/PCI](https://docs.kernel.org/7.1/PCI/acpi-info.html)
descrive MCFG e _CBA per ottenere le finestre ECAM, distinte dai bridge.
MCFG descrive accesso configurazione: da sola non e l'albero dei device.

[Intel3Series316966](https://www.intel.com/Assets/PDF/datasheet/316966.pdf),
§§4.5.1/4.5.2: CF8/CFC e il meccanismo legacy del chipset Q35; non offre
un campo segmento. La revisione repository mostra questo trasporto nel
lettore PCI kernel attuale. Il loader usa invece EFI_PCI_IO_PROTOCOL e
GetLocation, ma l'acquisizione della sola GPU rifiuta oggi Segment!=0.
Questi limiti non devono essere nascosti da un'API con parametro segment.

UEFI PCI IO e riferimento per l'alternativa loader:
[UEFI2.10, capitolo14](https://uefi.org/specs/UEFI/2.10/14_Protocols_PCI_Bus_Support.html).
I servizi firmware non possono restare callback dopo ExitBootServices.
Prima di nuovo codice hardware verificare la sezione precisa, larghezza,
riservati e indirizzamento di ogni accesso del trasporto scelto.

## Trasporto PCI scelto

N, scelto dall'utente: discovery e lettura native nel kernel tramite MCFG/ECAM.
Il kernel possiede mapping temporanei UC, RO supervisor/NX con guardie;
valida le finestre e legge soltanto i registri necessari. Nessuna
scansione dei BAR che ne modifichi il valore, nessuna assegnazione bus o
risorse. Il contratto boot puo restare v5 in questo blocco di routing;
la GPU nonzero-segment richiedera comunque estendere l'acquisizione GPU
attuale prima di provarne il percorso completo.

Richiede parser MCFG, autorita del mapping configurazione e rollback,
adattatori ACPI/paging e test. Non mappare l'intera ECAM come RAM normale.
_CBA/hotplug non viene dichiarato implementato solo leggendo MCFG. Le
piattaforme prive di una finestra supportata sono UNSUPPORTED, senza
ripiegare silenziosamente su CF8 per segmenti nonzero.

F: snapshot topologia raccolto dal loader tramite protocolli UEFI.
Riduce la nuova superficie MMIO del kernel durante il primo boot, ma
estende l'handoff oltre v5: memoria snapshot posseduta, dimensioni,
versione, copia/rivalidazione e durata dopo ExitBootServices. Nessun
puntatore a protocollo UEFI trasportato al kernel. Lo snapshot fotografa
il boot; per ricontrollare bridge o gestire hotplug servira comunque un
trasporto nativo successivo. Non basta che il loader abbia enumerato la
GPU: occorrono i bridge e la loro relazione con bus e segmenti.

La scelta B gia ricevuta riguarda la topologia ampia. N/F stabilisce il
nuovo contratto di accesso e quali moduli devono essere estesi; non e una
nuova domanda sul supporto di bridge e piu unita.

## Modello di dati e limiti proposti

Snapshot topologico posseduto e immutabile durante la selezione. Limiti
iniziali proposti:8 segmenti,32 finestre configurazione,512 funzioni
presenti,64 bridge, profondita8. Non sono limiti PCI universali. Superarli
produce un errore esplicito, mai uno snapshot troncato accettato.

Identita per funzione: segmento16bit, bus8bit, device5bit, function3bit,
vendor/device ID, tipo header e classe. Bridge: primary/secondary/
subordinate, parent verificato e relazione ai bus. Letture minime e
bounded, senza toccare registri device-specific o read-clear. Gli offset
effettivi devono essere citati dalla fonte primaria prima del codice.

MCFG range, numero di bus e radice topologica sono concetti distinti.
Una finestra di configurazione non autorizza a inventare il parent di un
device o a ritenere che tutti i bus formino un solo albero sul bus0.
Radici multiple nello stesso segmento devono essere rappresentabili.

L'albero richiede BDF unici, assenza di cicli, un solo parent per bus
downstream e intervalli coerenti: bridge fratelli non possono rivendicare
gli stessi bus, mentre gli intervalli degli antenati devono contenere
quelli dei discendenti. Un range numerico secondary/subordinate non
sostituisce la verifica degli effettivi passaggi per risolvere uno scope.

Nessuna rinumerazione del bus. Dispositivi o bridge che cambiano tra
lettura e verifica invalidano l'intero snapshot. Hotplug e mutazioni
concorrenti restano fuori dal primo profilo diagnostico: un contatore di
generazione software da solo non impedisce cambiamenti hardware.

## Resolver DMAR e precedenze

1. Rivalidare inventario DMAR, counts, riservati, appartenenza degli scope,
   identita GPU e snapshot topologico. OtherRecords!=0 resta un gate:
   RMRR/ATSR/altri record saltati non diventano autorizzati.
2. Per ogni scope PCI nel segmento interessato, partire dallo start bus
   e seguire le coppie device/function. Ogni passaggio intermedio deve
   essere un bridge valido nello snapshot; il successivo bus e quello
   secondario del bridge. Rifiutare path inesistenti, cicli o incoerenze.
3. Endpoint/type1: il terminale deve essere un endpoint e coincidere col
   requester GPU. Sub-hierarchy/type2: il terminale deve essere un bridge;
   includere bridge stesso e discendenti verificati, non un range assunto.
4. IOAPIC/type3 e HPET/type4 non sono match GPU. Non assimilarli a un
   endpoint anche quando la rappresentazione del source ID e simile.
5. Raccogliere i match espliciti prima di considerare include-all. Match
   in DRHD distinti, scope contraddittori o duplicati non accettabili sono
   INVALID: nessuna scelta del primo elemento o della base piu bassa.
6. In assenza di match esplicito, selezionare l'unico include-all dello
   stesso segmento, dopo aver escluso scope non risolti che potrebbero
   cambiare la precedenza. Unita di altri segmenti non sono fallback.
7. Pubblicare soltanto alla fine unit index, base/span, segmento, requester
   e motivo del match. Output disgiunto dagli input, invariato su errore.

Il BDF software non prova che il requester ID DMA sia identico in ogni
tipo di bridge. Alias dei bridge PCI convenzionali, ARI, SR-IOV, ATS e
PASID non sono implicitamente supportati: prima di accettare un caso
servono modello del requester e prove corrispondenti. Il primo profilo
bridge verificato dovra indicare gli esatti device QEMU utilizzati.

## Confini dei linguaggi e ownership

C possiede letture, memoria e rollback del trasporto. Rust puro gestisce
validazione del grafo e risoluzione di path/precedenze su slice bounded,
senza unsafe, allocazioni nascoste o dereference MMIO. L'FFI C espone
dimensioni e layout controllati; snapshot invalidi non entrano nel walk.
Assembly aggiuntivo non e richiesto per gli algoritmi topologici.

Il candidato e valido per lo snapshot verificato, non una capability DMA
riutilizzabile attraverso cambi di identita. Il futuro owner di dominio
ricontrollera routing, device e stato hardware prima della pubblicazione.

## Sequenza non eseguibile

    EMPTY -> CONFIG_ACCESS_OWNED -> SNAPSHOT_BUILDING
          -> SNAPSHOT_VERIFIED -> SCOPES_RESOLVED -> UNIQUE_CANDIDATE
          -> CONFIG_ACCESS_REVOKED -> RESULT

Errore durante acquisizione: nessun risultato, rollback e budget
verificati. Revoca incerta conserva le risorse possedute e fallisce.
Errore nel resolver: nessuna selezione parziale. Nessuno stato implica
ROOT_PUBLISHED, TRANSLATION_ACTIVE o GPU_DMA_ALLOWED.

## Accettazione e ordine di implementazione

Il lavoro ampio viene mantenuto come obiettivo; ogni modulo eseguibile
deve superare i propri gate prima del successivo, secondo direttive-locali §1.

1. Trasporto configurazione scelto: bounds, assenze, finestre sovrapposte,
   overflow, letture fallite, mapping/rollback e risorse trattenute se
   stop/revoca incerti. Dieci boot positivi consecutivi sul profilo finale.
2. Topologia: ponte singolo e annidato, piu radici, segmenti separati,
   sibling overlap, cicli, cambi tra letture, profondita/capacita superate,
   immutable rejection. Test Rust debug/release e C/ASan dove applicabile.
3. Routing: fino a8 unita e32 scope, match specifico contro include-all,
   GPU sotto bridge, scope irrisolvibile, duplicati e isolamento segmenti.
   Confronto indipendente host/guest dei dati usati dal resolver.
4. QEMU: topologia reale con GPU dietro root port/bridge e dieci boot per
   profilo positivo. Multi-DRHD sintetico e mock devono essere etichettati:
   non provano piu IOMMU hardware attive. Piu unita emulate reali vanno
   dichiarate solo se la versione QEMU permette il corrispondente setup.
5. Regressioni QI e console ordinaria; hash immagine/ESP, accounting,
   warning come errori, cppcheck, size/folder, fonti e diff finale.

## Passo successivo separato

Attivazione/fault seguono routing verificato. TE si applica all'intera
unita: va definita la sorte degli altri requester, senza passthrough
implicito o interruzione non valutata dei device attivi. Restano da
verificare DMA consentito/vietato, fault, revoca e invalidazione prima
del riuso RAM, poi ACCESS_PLATFORM e console VirtIO persistente.
