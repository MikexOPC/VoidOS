# K3-I — ingresso AP: design approvato per il diagnostico

30 ottobre 2026. Scelta A approvata dall'utente, inclusi i soli adattatori
necessari al diagnostico. Questo file descrive il progetto; avanzamento e
prove in [k3i-protocol.md](k3i-protocol.md). K3-H resta inattiva.

## Decisioni già acquisite

Riserva nel kernel, root condivisa, avvio sequenziale, risorse private
predisposte e rollback fino al primo tentativo di avvio. Panic SMP con NMI
diretti e attesa limitata. Non si riaprono queste scelte.

## Fonti primarie consultate

- [Intel SDM Vol. 3A, 253668-078US, dicembre 2022](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf),
  §10.8.5.1–3: preparazione IA-32e, strutture e cambio del formato IDT.
  §10.8.5.2 richiede di impedire interrupt/eccezioni nella finestra prima
  dell'IDT a 64 bit e specifica che gli NMI richiedono controllo esterno.
  §6.7: IF non maschera NMI. Non basta aggiungere CLI allo stub.
- [Linux v6.12 trampoline_64.S](https://github.com/torvalds/linux/blob/v6.12/arch/x86/realmode/rm/trampoline_64.S):
  separazione dei percorsi real/protected/64 bit e salto al kernel alto.
  GPL-2.0, riferimento concettuale; nessun codice copiato. Le scelte Linux
  su IDT e arresto non sostituiscono il contratto locale.
- [K3-H](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu): root sorgente da ricontrollare/ricostruire
  al punto d'uso; nessun CR3 AP già verificato in esecuzione.
- [K3-F](k3f-smp-design.md): ammissione al panic soltanto con record,
  TSS/IST e ingresso NMI validi. Un AP in transizione non è ancora ammesso.

Il trasporto INIT/SIPI richiede una scheda hardware separata prima del
codice: edizione SDM, sezione MP initialization, campi ICR, sequenza,
delivery status, clock, timeout e controllo errori. Non riusare valori
di ritardo o numeri di sezione da edizioni differenti senza verifica.

## Riscontro sulle interfacce esistenti

transition/transition.h espone prepare/verify/cancel, ma non un commit
congiunto né un ingresso AP. resources/resources.h espone il commit delle
risorse private; questo da solo non sigilla codice, dati e root condivisa.
percpu/internal.h espone preparazione e seal AP, non una promozione runtime
AP verificata. Il nuovo protocollo deve completare questi confini.

## Proposta A: primo avvio nel diagnostico dedicato

Un AP alla volta entra, dimostra identità e contesto privato, pubblica una
conferma atomica e resta parcheggiato senza scheduler o accessi ai driver.
Dopo la conferma di uscita dalla memoria transitoria si procede al prossimo.
La prova positiva finale richiede BSP più tre AP realmente presenti.
Il parcheggio terminale usa HLT; non è un polling operativo senza limite.

Ownership: C possiede lease, preparazione e commit; Rust valida il record
di ingresso e le transizioni; Assembly gestisce modi CPU, stack e ABI.
Il formato del blob e lo strumento di assemblaggio misto 16/32/64 bit
vanno verificati con la toolchain disponibile prima di fissarne la build.
Un eventuale nuovo assembler diventa una dipendenza esplicita.

Sequenza concettuale, non codice eseguibile:

    PREPARED -> VERIFIED -> COMMITTED -> START_SENT -> ENTERED
                                              -> PRIVATE_READY -> PARKED
                                    errore/timeout -> QUARANTINED

COMMITTED precede il primo comando che potrebbe far partire l'AP. Da quel
punto codice, dati, tabelle e risorse private non vengono liberati. Dopo
timeout non si riscrivono i dati condivisi per un altro AP: potrebbe esserci
un ingresso tardivo. Il diagnostico termina con errore e risorse trattenute.

Il record d'ingresso deve contenere versione/dimensione, generazione,
identità attesa, root transitoria e operativa, stack e riferimenti privati.
Layout e offset condivisi sono verificati nella build. Rifiutare overflow,
puntatori non canonici, campi riservati e capacità insufficienti. Il BSP
pubblica con release, l'AP legge con acquire e conferma una sola generazione.

Prima del commit: nessun lock posseduto, finestra fisica chiusa, verifica
dei mapping e congelamento delle modifiche BSP durante la transizione.
Prima di chiamare C: CR3 operativo, stack privato, ABI MSVC e stato CPU
coerente con le istruzioni emesse dal compilatore, GDT/TSS/IDT e GS validi.
Non usare la vista ricorsiva con la root transitoria che ne è priva.

## Gate ancora da risolvere nel design eseguibile

La finestra IDT/NMI richiede una soluzione documentata per il profilo
QEMU scelto; trattenere gli NMI di panic del BSP fino all'ammissione non
dimostra protezione da ogni NMI esterno. Non dichiarare supporto hardware
generale né avviare AP prima di risolvere e verificare questo prerequisito.
Gestire anche fault iniziali senza dipendere da GS non ancora installato.

Il primo blocco implementativo deve fissare blob, rilocazioni ammesse,
indirizzi iniziali, IDT dei diversi modi, capacità pagina codice/dati e
punti di conferma. Il limite di una pagina non giustifica omissioni: se
insufficiente, discutere l'estensione prima di cambiare l'allocazione.

## Alternativa B e scelta richiesta

B collega subito l'avvio al boot ordinario. Accorcia il passaggio successivo,
ma espone immediatamente console e driver al nuovo lifetime multicore.
A isola le prime prove reali di ingresso e arresto; è la proposta consigliata.
Nessuna delle due implica scheduler SMP, preemption o TLB shootdown: K4
rimane distinto. Il profilo ordinario sarà collegato dopo il gate AP.

Perimetro proposto: per-CPU/ingresso e soli adattatori necessari a LAPIC,
descrittori/eccezioni, bootstrap, build e test centralizzati. Nessuna
revisione generale dei driver. Scelta A acquisita secondo direttive-locali §7.

## Accettazione prevista, non risultati

- Host C/Rust: record valido, errori di identità/generazione, overflow,
  transizioni duplicate, commit parziale, timeout e ingresso tardivo.
- Audit blob: disassemblaggio nei tre modi, dimensioni, offset e assenza
  di dipendenze runtime/indirizzi Windows; sorgenti entro i limiti locali.
- QEMU: almeno dieci boot consecutivi dell'immagine finale con identità,
  stack/GS/CR3 e avanzamento di ciascun AP realmente osservati.
- Negativi separati: conferma assente/errata, fault iniziale, AP tardivo;
  nessun riuso della memoria condivisa dopo tentativo irrisolto.
- Panic SMP: NMI remoto, conferme dei core e core mancante alla deadline.
- Regressione ordinaria e console pertinente; sanitizer/accounting host
  distinti dall'accounting fisico. Hardware reale e coverage restano N/D.

Verifica di questa stesura: lettura delle interfacce sopra e delle fonti
collegate; nessuna build o QEMU eseguiti per la modifica documentale.
