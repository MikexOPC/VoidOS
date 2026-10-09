# Dispatcher separato e idle riutilizzabile: contratto v0

8 ottobre 2026. Miglioramenti autorizzati sul modulo scheduler; nessuna nuova
policy di preemption, priorita, SMP o teardown di driver. Estende il
[contratto di arresto](persistent-design.md) e mantiene le scelte precedenti:
eventi con generazione, wake-all, IRQ post separato, dispatcher BSP cooperativo.

## Confini

dispatcher/engine.c gestisce SELECT, comandi delle task, EXIT/COLLECT, drain
delle notifiche e BLOCKED attraverso una configurazione fidata. Non conosce
profili, due task, contatori diagnostici o driver. Il core Rust mantiene gli
stati/generazioni/attese/stop; nessun riferimento Owner viene trattenuto da C.

diagnostic/profiles.c contiene entry point, limiti, marker di accettazione e
invarianti dei workload. integration/kernel.c conserva contesti, mapping,
lease e callback native. I risultati dei comandi sono consegnati alla mailbox
dell'attore; una richiesta stop puo riguardare un handle target distinto.

Configurazione privata: da una a otto task gia pubblicate, limite dispatch
1..65536 coerente col numero di task, limite idle 0..1024, callback native
immutabili e statistiche possedute/azzerate. Non e un'API per user pointer.
Il guard C impedisce rientro ricorsivo, sotto contratto BSP/IF=0; non e un lock
SMP. Il dispatcher puo attivare IF solo dentro i backend gia verificati.

## Ordine e failure

Pseudocodice non eseguibile:

```text
verifica contratto e acquisisci guard di rientro
ripeti fino alla raccolta delle task della sessione:
    verifica contesto dispatcher e budget di avanzamento
    SELECT Rust
    BLOCKED: invoca idle supervisionato entro il suo limite, poi ripeti SELECT
    RUNNING: valida contesto/lease nel backend e switch
    al ritorno: applica comando Rust, consegna risultato alla mailbox
    EXIT: rilascio native dal dispatcher, poi COLLECT Rust
    libera binding attore e drena notifiche dove il profilo lo richiede
verifica EMPTY e rilascia guard
```

Un idle che restituisce successo senza rendere READY alcuna task non certifica
progresso: SELECT viene ripetuto entro il limite idle. Un budget di dispatch
esaurito viene rifiutato prima di un nuovo SELECT, evitando una transizione
RUNNING non eseguita. Fallimenti di run/reply/collect/release mantengono lo
stato gia pubblicato e le risorse ancora possedute; non effettuano rollback
impossibili di contesti o cleanup automatico. Il caller nativo termina il
profilo; non interpreta il return come autorizzazione a riprovare o liberare.

## Riuso hardware limitato ma reale

Due sessioni idle, alla seconda e terza consegna LAPIC. Entrambe osservano
l'epoch corrente dell'evento 3 prima di WAIT: la seconda riusa l'evento gia
segnalato, senza assumere epoch zero. Ogni sessione ha due lease pinnate,
arm/HLT supervisionato HPET, wake-all, raccolta ed end del prestito contabile.
Il riarmo successivo avviene dopo consumo/quiescenza del driver.

Il backend idle riceve source/goal/event/run della sessione e azzera i soli
contatori locali dopo aver verificato che nessun binding precedente e vivo.
Un fallimento conserva il binding e vieta retry/rebind; wrapper diagnostici
mantengono la loro sequenza finita. Epoch e stato Owner non sono azzerati.
I tempi sono misurati dentro le sessioni, escludendo preparazione/raccolta
degli stack; la latenza idle include ancora il drenaggio HPET v0.

Non si introduce alcun registro o nuova istruzione. Le API timer/HPET e i
relativi contratti hardware gia verificati restano invariati; gli agganci
LAPIC riusano il risultato della terza attesa senza eseguirla due volte.
Il backend hardware resta limitato a due stack e a questa source: l'interfaccia
generica del dispatcher non certifica automaticamente otto lease o driver
arbitrari. Servono contratti distinti di binding, payload e accounting.

## Gate

Host: motore reale C con Owner Rust, sessioni da 1/2/8 task e stop cooperativo;
callback/CPU/resources mock espliciti. Errori: mancato wake, comando invalido,
contesto perso, reply/collect/release rifiutati, budget e rientro ricorsivo.
Adapter idle: due sessioni con epoch aggiornato e rifiuto di chiusura del
prestito nella seconda, senza dichiarare cleanup di un binding fallito.

Firmware: dieci boot dell'immagine finale con due HLT hardware verificati,
budget/PTE/CR3 e stack ripristinati, regressioni negative e ciclo continuo
con stop/cleanup. Coverage e hardware fisico restano N/D se non misurati.
Non avanzare a un nuovo driver prima dei gate applicabili.
