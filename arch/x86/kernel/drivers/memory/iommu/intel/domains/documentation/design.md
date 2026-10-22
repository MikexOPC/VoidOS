# Dominio DMA Intel VT-d v0: contratto approvato

20 ottobre 2026. Stato: contratto A approvato dall'utente e implementato
nel diagnostico; [prove e limiti](validation.md).
Prerequisito verificato: [mapping e capacita](../../registers/documentation/validation.md).
La scelta di questo contratto ha preceduto l'implementazione, secondo direttive-locali §7.

## Ricerca e motivazione

Fonte primaria: [Intel VT-d rev. 5.0, D51397-017](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf).
La copia verificata e in `kernel/build/iommu-research/vtd-831418.pdf`,
SHA-256 `A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6`.

- §3.10: ECAP.C governa lo snoop delle strutture in modalita legacy.
  Con C=0 la visibilita attraverso le cache CPU non e garantita.
- §§6.1, 6.5 e 6.8: caching mode, invalidazioni e write buffer sono
  requisiti distinti dalla pulizia delle cache CPU.
- §§9.1 e 9.3: root entry e context entry sono da 128 bit; il contesto
  sceglie dominio e radice di traduzione. AW=2 seleziona quattro livelli.
- §9.8: le entry second-stage hanno permessi R/W nei bit 0/1. Non sono
  PTE CPU: niente NX, PAT o flag supervisor presi dal mapper dei registri.

[Linux v6.18, intel/iommu.c](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/iommu/intel/iommu.c):
allocazione e pulizia delle strutture sono separate dalla pubblicazione;
le pagine ritirate attendono l'invalidazione prima del rilascio. Si adotta
questa distinzione di lifetime, non le strutture generiche di Linux.
[QEMU v11.1.0, intel_iommu.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/i386/intel_iommu.c)
e il riscontro del modello emulato; non sostituisce la specifica Intel.
Nessun codice di tali progetti viene copiato; licenze GPL restano loro.

## Alternative

A, proposta v0: un dominio privato per una GPU, preparato e verificato
inattivo. Indirizzi DMA dedicati, distinti dai fisici. Si collaudano
formato, ownership e rollback prima di esporli al dispositivo.

B: gestore generico di domini per molti dispositivi, con assegnazione
dinamica degli indirizzi e durata concorrente. Serve piu infrastruttura
PCI, sincronizzazione e revoca; amplia molto il primo blocco.

L'identita IOVA=fisico sarebbe piu semplice per i chiamanti ma renderebbe
piu facile confondere gli spazi di indirizzamento. La proposta usa IOVA
espliciti fin dall'interfaccia. L'attivazione non fa parte di A o B in
questo documento: richiedera un proprio contratto e prove DMA reali.

## Contratto proposto per A

Un owner BSP non spostabile; un solo dominio con ID 1, una root legacy e
un context per una sola funzione PCI. Tutte le altre entry sono assenti.
ID 0 non viene usato. Quattro livelli, pagine da 4 KiB, nessun pass-through,
ATS, PASID, pagina grande o aggiornamento concorrente.

Il costruttore riceve una identita segmento/bus/device/function gia
validata e un elenco esplicito di lease pinned. Non deduce il routing dai
soli scope DMAR; l'associazione definitiva DRHD-dispositivo e un gate
prima dell'attivazione. Segmenti diversi da zero e routing ambiguo sono
rifiutati dal profilo iniziale, senza fallback a identita arbitraria.

Proposta di limiti: massimo 16 intervalli, 4.100 pagine DMA complessive
(16 MiB di pixel piu quattro pagine di code), in una finestra IOVA da
32 MiB con base 0x01000000. Pagina assente prima, fra e dopo gli intervalli;
anche con 17 guardie bastano nove page table finali. Massimo 14 pagine di
strutture: root, context, tre livelli superiori e nove tabelle finali.
Si allocano solo quelle necessarie; i limiti non sono risultati misurati.

Intervalli fisici e IOVA devono essere allineati, disgiunti e senza overflow;
ogni foglia deve riferire una pagina del lease richiesto. Escludere registri,
framebuffer firmware, tabelle CPU/DMA e RAM non posseduta. Permessi espliciti
R oppure R/W in base alla direzione DMA; niente autorizzazione automatica
dell'intera RAM. Le tabelle DMA non sono buffer accessibili al dispositivo.

Rust puro prepara geometria ed entry e controlla gli indici. C mantiene
lease, pinning, scritture bounded e readback tramite la finestra RAM
esistente. Test centralizzati nel percorso speculare sotto `test/arch/x86/`.
Le entry da 128 bit vengono scritte come due parole da 64 bit soltanto
perche sono private e non raggiungibili dal dispositivo. Questo writer
non e un updater di contesti attivi; non garantisce aggiornamento atomico.
Eventuali adattatori a memoria, build e probe saranno limitati a questo
collaudo, senza cambiare la console ordinaria o il boot ABI.

## Stati, coerenza e failure

Sequenza non eseguibile:

    EMPTY -> VALIDATED -> ALLOCATED -> BUILT_PRIVATE -> VERIFIED_PRIVATE
      ^                      |              |                 |
      +------ rollback/release senza pubblicazione hardware --+

`VERIFIED_PRIVATE` significa formato e ownership verificati, non pronto
per DMA. Il blocco non scrive RTADDR/GCMD e non produce uno stato ACTIVE.
Un errore conserva l'owner se il cleanup non e dimostrato; nessun PASS
con budget diverso da quello iniziale o prestito RAM ancora aperto.

Prima di un futuro stato PUBLISHABLE occorrono: pulizia delle cache per
C=0 con istruzioni e prerequisiti CPU verificati, ordering, trattamento
RWBF e invalidazioni previste dal profilo. Readback CPU, compiler barrier
e UC dei registri non bastano. La preparazione privata non risolve questi
requisiti e non modifica il giudizio `Compatible=0` del lettore attuale.
Dopo un futuro uso hardware, rilascio solo a device fermo e revoca/drain
confermati; il rollback privato non va riutilizzato come teardown attivo.

## Criteri di accettazione concordati

- Unit test Rust/C: entry esatte, riservati, limiti, permessi, guardie,
  indirizzi diversi IOVA/fisico, overflow e alias rifiutati.
- Allocatore reale nei test host; failure di ogni allocazione/pin/borrow,
  output immutati su errore e budget ripristinato. ASan e accounting distinti.
- Diagnostico isolato: costruzione, walk software indipendente, rilascio,
  ricostruzione; scanner paging CPU e finestra verificati prima/dopo.
- Dieci boot QEMU consecutivi sull'immagine finale; dump delle strutture
  confrontati dal test host, GSTS invariato, regressioni applicabili.
- Nessun accesso DMA consentito/vietato dichiarato provato da un walk CPU.
  Tali prove richiedono l'attivazione e un dispositivo che emetta richieste.

## Passi successivi separati

1. Coerenza delle strutture, routing DRHD e protocollo comandi/invalidation.
2. Attivazione diagnostica, fault osservabili e richieste DMA ammesse/vietate.
3. Integrazione VirtIO con indirizzi DMA espliciti e ACCESS_PLATFORM,
   poi durata persistente, recovery e console ordinaria.

Il presente documento non assegna percentuali di completamento o stime
di prestazioni e non costituisce evidenza di implementazione.
