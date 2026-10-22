# Coerenza e invalidazioni VT-d: contratto v0

21 ottobre 2026. Scelta utente: coda hardware di invalidazione.
Prerequisito: [dominio privato verificato](../../domains/documentation/validation.md).
Implementazione e limiti delle prove sono registrati nel [report](validation.md).

## Fonti verificate

[Intel VT-d rev. 5.0, D51397-017](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf):
§§3.10, 6.5.1/2, 6.5.2.9, 6.8, 11.4.4 e 11.4.9.
La copia locale mantiene SHA-256
`A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6`.

- ECAP.C=0 richiede considerare la visibilita delle strutture attraverso
  le cache CPU; questa operazione non sostituisce le invalidazioni IOMMU.
- La coda supportata da ECAP.QI funziona anche con traduzione disabilitata.
- Il wait con SW scrive un DWORD coerente in RAM. Head=tail da solo non
  dimostra il completamento delle invalidazioni precedenti.
- Lo stop richiede coda vuota e ultimo wait completato, poi QIES spento.
  Le invalidazioni richieste implicano il write-buffer flush previsto
  per RWBF; una pulizia delle cache CPU da sola non lo sostituisce.

[Intel SDM 2A, 253666-093US](https://cdrdv2.intel.com/v1/dl/getContent/671199),
settembre 2026, CLFLUSH pp. 3-144/145: CPUID.1.EDX[19], geometria tramite
CPUID.1.EBX[15:8]. [SDM 2B, 253667-093US](https://cdrdv2.intel.com/v1/dl/getContent/671241),
MFENCE p. 4-15: prerequisito CPUID.1.EDX[26]. I compiler barrier non
sono sostituti delle istruzioni CPU.

[Linux v6.18, driver Intel](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/iommu/intel/iommu.c)
resta un confronto per separare cache maintenance, comandi e lifetime;
[QEMU v11.1.0](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/i386/intel_iommu.c)
e il modello del collaudo. Nessuna copia di implementazioni o assunzione
che un completamento emulato provi la coerenza di un IOMMU fisico.

## Alternative discusse

A, scelta: coerenza CPU e queued invalidation, senza pubblicare il
dominio o abilitare la traduzione DMA. Due pagine RAM possedute per coda
e completion, una sola operazione in volo. Prepara il percorso futuro.

B: coerenza CPU e invalidazioni tramite registri legacy. Evita la coda
RAM ma ha funzionalita piu limitate e richiede un successivo backend QI
per estensioni quali scalable mode. La specifica limita questa interfaccia
alle revisioni hardware con major version fino a 5. Non e la proposta v0.

In entrambi i casi C=0 non viene ignorato. Nessun fallback silenzioso
fra interfacce o abilitazione DMA per far passare un test.

## Contratto implementato per A

Profilo diagnostico isolato, BSP, una unita Intel VT-d in legacy mode.
Ingresso solo da stato inattivo verificato; rifiutare comandi pendenti,
stato firmware gia attivo o capacita mancanti, senza appropriarsene.
Non scrivere RTADDR o TE. La GPU non riceve comandi DMA da questo blocco.

Il modello QEMU mantiene un indirizzo IQA latched: prima dell'enable il
readback puo restituire il vecchio indirizzo, dopo lo stop puo conservare
quello appena usato. Il readback della programmazione avviene quindi dopo
QIES=1. La preparazione accetta un precedente IQA allineato soltanto con
QIES=0, head/tail zero e nessun comando o errore pendente: non adotta, legge
o libera la vecchia RAM. Dopo wait e QIES=0 scrive IQT/IQA zero; accetta
readback IQA zero oppure l'esatto indirizzo posseduto, mai un terzo valore.
La traccia QEMU verifica separatamente le scritture finali zero. Questa
gestione deriva da vtd_mem_read/vtd_handle_gcmd_qie della versione citata;
il readback residuo non prova una coda ancora attiva.

Coerenza: verificare CPUID e geometria, poi pulire le pagine possedute
attraverso la finestra WB gia esistente e ordinare con MFENCE. Nessun
WBINVD globale o modifica del PAT. Il piano delle linee e bounded e
validato in Rust; il backend C esegue le istruzioni dopo i controlli.
Una pulizia completata non e un'autorizzazione riutilizzabile dopo che
le strutture sono state modificate: il futuro publish deve ricontrollarle.

QI: una pagina da 4 KiB con 256 descrittori da 128 bit e una pagina
separata per lo status, entrambe da lease pinned e disgiunte da tabelle
e buffer GPU. Tre descrittori per transazione: context-cache globale,
IOTLB globale, wait con status e fence. Nessun interrupt di completion,
PASID o ATS; riservati azzerati. Un produttore, nessun batch concorrente.

Testare anche il wrap del ring, mantenendo sempre libera almeno una
entry. La pubblicazione della tail avviene dopo scrittura e visibilita
dei descrittori. Il completamento deve corrispondere al token atteso;
non accettare uno status vecchio o il solo avanzamento della head.

Attese sul clock PM validato gia disponibile, con deadline e limite
finito di letture. Clock fermo, timeout o errori IOMMU non sono PASS.
Un errore impedisce nuove richieste fino a stop/recovery verificati.

Il mapping registri corrente RO deve acquisire un modo RW autorizzato
esplicitamente per questo owner. Rimangono UC, supervisor, NX e guardie.
Una whitelist distingue letture e scritture dei registri; nessun puntatore
generico scrivibile esposto al chiamante. L'inventario deve verificare
anche questa autorita e continuare a rifiutare alias non autorizzati.
Il modo RO esistente e la console ordinaria conservano i loro contratti.

Registri interessati da dettagliare nel codice con maschere e sezioni:
GCMD 0x18/u32, GSTS 0x1C/u32, FSTS 0x34/u32, IQH 0x80/u64,
IQT 0x88/u64, IQA 0x90/u64. QIE/QIES sono il bit 26 di GCMD/GSTS.
Stato dei comandi mantenuto in uno shadow verificato, senza read-modify-write
del registro comandi. Nessuna scrittura W1C generica su FSTS.

## Stati e failure

Sequenza non eseguibile:

    EMPTY -> OWNED -> CPU_VISIBLE -> QI_ENABLED -> SUBMITTED -> COMPLETED
                                    ^                           |
                                    +--------- batch -----------+
    COMPLETED -> QUIESCED -> QI_DISABLED -> RELEASED

Nessuno stato implica DMA_TRANSLATION_ACTIVE. Dopo enable incerto o
timeout, queue/completion e mapping rimangono posseduti finche lo stop
non e confermato. Non liberare per recuperare artificialmente il budget.
Il teardown privato del dominio non sostituisce questo stop hardware.

## Perimetro e prove di accettazione

Modulo `invalidation/`, con i soli adattatori necessari a domini,
registri/inventario, build e probe. C per ownership e hardware; Rust puro
per geometria, descrittori e transizioni. Test in percorso speculare
centralizzato; script nel modulo, artefatti sotto build/.

1. Coerenza CPU: feature mancanti, geometrie invalide, estremi di pagina,
   ordine delle operazioni e nessun accesso fuori dalle pagine possedute.
2. QI host: descrittori esatti, wrap, token stantii, timeout, errori,
   stop incompleto e risorse trattenute; ASan e accounting separati.
3. QEMU: completamenti reali del motore QI, confronto esterno di registri
   e RAM, almeno dieci boot consecutivi dell'immagine finale e assenze.
4. Regressioni dei profili RO, dominio privato e console ordinaria;
   cleanup, budget, fonti, dimensioni e diff finale verificati.

Il collaudo senza radice pubblicata prova il protocollo di invalidazione,
non la rimozione di traduzioni DMA effettivamente usate. Quest'ultima
richiede il blocco successivo con richieste consentite/vietate e fault.
Restano separati routing DRHD, gestione fault e integrazione VirtIO
ACCESS_PLATFORM; nessuna estensione dell'eccezione DMA della console.
