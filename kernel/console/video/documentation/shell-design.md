# Console operativa della shell: design approvato, 10 ottobre 2026

## K4-C5: proposta SMP, in attesa della scelta

Il servizio attuale non e sicuro per chiamate concorrenti: `Busy` in
`service/console.c` rileva il rientro, ma non e atomico e non sostituisce
un lock. Le garanzie BSP descritte sotto restano il contratto implementato.
Questa sezione e un progetto, non una prova di supporto SMP.

Riferimento: [Linux v6.12, Lock types, CPU local locks](https://www.kernel.org/doc/html/v6.12/locking/locktypes.html#cpu-local-locks).
Disabilitare interrupt o preemption protegge soltanto la CPU locale.
Il riferimento motiva la separazione; non si importano lock o codice Linux.

### Alternative e raccomandazione

A, consigliata: il worker BSP resta unico proprietario della griglia e
del renderer. I produttori inviano richieste copiate in una FIFO limitata.
Il lock protegge soltanto la coda; nessun disegno, callback, wait o yield
avviene sotto quel lock. Mantiene sul BSP i vincoli hardware esistenti.

B: un lock per servizio permette mutazioni sincrone da qualsiasi CPU.
Richiede anche separare il prestito al renderer dalle mutazioni e definire
snapshot/ack delle celle cambiate mentre il renderer lavora. Tenere il
lock durante il disegno farebbe attendere gli AP per tutta la verifica
paging e la copia video. Non basta rendere atomico `Busy`.

### Contratto proposto per A

- Prima parte: sola coda e consumer controllato in host, senza collegare
  ancora i task AP alla console ordinaria. Modulo `kernel/console/video/`,
  relativi test centralizzati e script; nessun nuovo accesso hardware.
- Otto richieste, ciascuna con massimo 256 byte UTF-8 posseduti dalla coda;
  nessun puntatore del produttore viene conservato. Operazioni WRITE, EDIT
  e SUBMIT, con validazione completa prima della pubblicazione.
- Rust gestisce contenuto, indici, stati e validazione. Il gateway usera
  il lock kernel gia esistente, con ordine da verificare prima del binding.
  Nessuna assunzione che `&mut` o un booleano rendano sicuro il gateway FFI.
- Una richiesta entra interamente oppure viene rifiutata. Coda piena:
  FULL, nessuno scarto silenzioso e nessuna attesa attiva illimitata;
  il chiamante conserva il lavoro e riprova fuori dai lock.
- Ordine globale uguale all'ordine di pubblicazione sotto lock. WRITE
  e EDIT validano UTF-8; EDIT non accetta controlli. SUBMIT non ha payload.
  La coda non sostituisce la FIFO dei comandi della shell.
- Il consumer copia una richiesta in memoria privata, rilascia il lock,
  poi applica l'operazione. Accettata significa accodata, non disegnata.
  Una sola richiesta estratta puo essere in lavorazione; non viene ritentata
  dopo un errore parziale di applicazione.
- Stati OPEN -> DRAINING -> CLOSED oppure FAILED terminale. DRAINING
  rifiuta nuovi invii; CLOSED richiede coda vuota e nessun lavoro in corso.
  FAILED conserva la diagnosi e non consente riuso della sessione live.
- La RAM della sessione resta viva fino al distacco di tutti i produttori
  e del consumer. IRQ/NMI non chiamano la coda; i task non accedono
  direttamente alla griglia. La seriale di emergenza resta un percorso separato.

### Accettazione e integrazione successiva

Test host: FIFO, wrap degli indici, piena/vuota, UTF-8 italiano e invalido,
dimensioni 0/256/257, copia indipendente dal produttore, drain, errore
terminale e nessuna mutazione dopo rifiuto. Prima del collegamento SMP:
binding FFI sotto lock verificato, produttori concorrenti, preemption,
assenza di risvegli persi e gestione FULL senza bloccare il consumer.

L'integrazione richiedera un perimetro concordato per worker, chiamanti
input/shell, wakeup scheduler e build/test. Non e inclusa implicitamente
nel solo nucleo della coda. Gate finale: dieci boot consecutivi del
profilo SMP, ordine output, input attivo, cleanup e budget invariati.
Finche questi gate non passano, console e driver restano vincolati al BSP.

## Contratto BSP implementato

Scelta utente: servizio unico, aggiornamenti RAM serializzati (alternativa A).
Implementazione nel profilo esplicito `-ConsoleShell`; risultati e limiti nel
[report di verifica](shell-validation.md). Questo file conserva il design;
non sostituisce le prove del report.
Modulo: `kernel/console/video/`; sole interfacce necessarie con input,
shell, gateway video e inventario delle risorse della sessione.

## Evidenza attuale e riferimento

La griglia ammette un proprietario e non mutazioni concorrenti. Task0
gestisce editor/input, task2 esegue i comandi, task1 ospita la finestra
Assembly IRQ verificata. Il rendering operativo passa dal backend
`arch/x86/kernel/drivers/video/framebuffer/live/group.c`: verifica i lease
stack pinned correnti e prepara una vista attesa per il gruppo, poi ripristina
Inventory. Il numero di pagine deriva dai lease verificati; non e fisso a 48.
`test/kernel/console/video/active/snapshot.c` resta un adattatore diagnostico.

[Linux v6.12 fbcon, Attaching, Detaching and Unloading](https://www.kernel.org/doc/html/v6.12/fb/fbcon.html)
separa console, console framebuffer e driver, con vincoli sul distacco.
Il riferimento riguarda separazione e lifetime; non si copia codice,
locking Linux o supporto SMP nel nostro profilo BSP cooperativo.

## Alternative per il proprietario

A, scelta: servizio console unico, con aggiornamenti RAM sincroni e
serializzati a IF=0. Input e shell chiamano API distinte; non ricevono
puntatori mutabili alla griglia. Nessun yield, wait o callback esterna
durante una mutazione. Rientro rifiutato prima di modificare lo stato.
Soltanto task1 disegna un gruppo e torna alla finestra IRQ esistente.
Non si crea una task o una coda di rendering aggiuntiva.

B: task1 possiede anche tutte le mutazioni RAM, ricevendo operazioni
copiate in una nuova FIFO limitata. Richiede ordine fra produttori,
backpressure, eventi dati/spazio, chiusura e gestione della coda piena.
Questo costo ha senso se serve disaccoppiare i produttori; oggi non
esistono SMP o preemption che lo rendano necessario.

La precedente scelta griglia in RAM e gruppi da massimo 8 celle resta
valida. La scelta nuova riguarda l'accesso operativo da due task diverse.

## Invarianti della scelta A

- Un owner originale possiede griglia, stato della riga visualizzata,
  renderer e lifetime della sessione. Nessuna operazione dagli IRQ.
- Rust valida UTF-8/scalar e transizioni; C gestisce buffer, owner e
  integrazione. Assembly e politica IRQ non cambiano.
- L'editor esistente rimane autorevole: carattere rifiutato per limite
  non viene mostrato; Backspace a riga vuota non cancella il prompt.
- La riga in modifica e distinta dal testo dei risultati. Se una risposta
  arriva mentre si digita la riga successiva, si conserva e ridisegna la
  riga corrente dopo la risposta. Nessun obbligo nuovo di scartare tasti.
- Enter confermato conserva ordine con la FIFO comandi esistente; righe
  troncate restano rifiutate. Nessuna esecuzione anticipata per il video.
- Ogni input testuale espone capacita e lunghezza; UTF-8 incompleto o
  invalido non causa accessi oltre il buffer. Le attuali divisioni seriali
  in blocchi non vanno trattate come stringhe UTF-8 complete indipendenti.
- Disegno limitato a un gruppo di massimo 8 celle sulla stessa riga;
  controlli completi per gruppo e accounting temporale esistenti.
- I turni del worker devono continuare finche esistono celle dirty,
  anche senza nuovi byte input. Observe/recheck/wait evita risvegli persi.
- Prima del disegno, verifica operativa degli stack live con lease,
  pin, identita, generazioni e inventario pertinente. Il numero 48 da solo
  non autorizza mapping. Nessuna autorizzazione memorizzata tra gruppi.
- Stop impedisce nuove richieste; il lavoro video viene completato oppure
  esplicitamente annullato prima della raccolta degli stack. Nessun uso di
  owner/lease dopo close. Un errore di disegno rimane terminale.
- Framebuffer assente: seriale operativa. Autorita video presente ma
  invalida: errore, senza fallback che nasconda la violazione.
- IF=0 e cooperazione valgono soltanto sul BSP; nessuna garanzia SMP,
  watchdog per stallo C a IF=0 o promessa di servizio illimitato.

## Sequenza non eseguibile

```text
bind video opzionale -> prepara owner console -> registra sessione live
input accettato -> aggiorna vista della riga -> rendi worker eseguibile
Enter consegnato -> conferma riga -> FIFO comandi gia esistente
shell -> risposta limitata -> aggiorna transcript e ridisegna riga corrente
worker -> verifica risorse live -> disegna <=8 celle -> verifica/ack
       -> finestra IRQ -> yield oppure observe/recheck/wait
stop -> chiudi producer -> completa/annulla video -> revoca sessione
     -> collect stack -> verifica baseline -> cleanup
```

## Blocchi e criteri di accettazione

1. Owner e presentazione in RAM: test positivi/negativi, copied owner,
   rientro, Unicode italiano, riga piena, Backspace, risposta intercalata,
   wrap/scroll, stop e nessuna mutazione dopo rifiuto.
2. Autorizzazione operativa: stack/lease stale, pin assente, generazione e
   budget errati, teardown, snapshot originale conservato. Non spostare
   semplicemente l'adattatore diagnostico nel codice di produzione.
3. Collegamento input/shell/worker: prompt, help/echo/status/exit, backlog
   dirty senza input, ordine FIFO, seriale e stop con risorse raccolte.
4. Suite C/Rust con warning come errori, ASan C e accounting, analisi
   statica e diff; dieci boot consecutivi del profilo finale con video,
   verifiche pixels/output, IRQ, perdite e cleanup, piu fallback senza video.

## Costi e prerequisiti dell'implementazione

Owner del servizio: 67416 byte; vista: 66360 byte. Totale fisso 133776 byte,
senza heap. Transcript e riga corrente restano distinti: comporre la vista
costa O(colonne * righe + lunghezza riga), non soltanto O(celle modificate).
Il renderer scrive solo le celle dirty. Questo costo RAM/CPU e esplicito;
non e ancora un terminale ottimizzato per risoluzioni o carichi arbitrari.

Init richiede oggetti originali non pubblicati, distinti e buffer vivi.
Solo il servizio muta la vista; il renderer ne prende l'accesso esclusivo
fino a fine gruppo. Le API non provano la validita di un puntatore kernel
arbitrario e non sono user copy. Niente reinit di una sessione live, SMP
o accesso dagli handler IRQ.

Il gateway operativo verifica il pool/root originali, le lease pinned
degli stack live e il budget, poi presta uno snapshot locale soltanto al
controllo completo del gruppo. Ripristina il puntatore originale anche
se il disegno fallisce. Non conserva autorizzazioni fra gruppi; un errore
di disegno/ownership rimane terminale. Il solo ritardo di un gruppo gia
verificato produce WARN e accounting, come scelto dall'utente nel
[contratto aggiornato](measured-design.md). Il numero di pagine e derivato dagli owner live, non
dal valore diagnostico fisso 48.

Nessuna sequenza hardware nuova: restano mapping UC/RW/NX, clock PM e
finestra Assembly gia documentati nei contratti
[framebuffer](../../../../arch/x86/kernel/drivers/video/framebuffer/documentation/mapping.md)
e [runtime input](../../../../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md).
