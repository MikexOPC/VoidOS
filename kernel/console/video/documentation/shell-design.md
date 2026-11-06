# Console operativa della shell: design

## K4-C5: coda con owner BSP, scelta approvata il 6 novembre 2026

Il servizio attuale non e sicuro per chiamate concorrenti: `Busy` in
`service/console.c` rileva il rientro, ma non e atomico e non sostituisce
un lock. Le garanzie BSP descritte sotto restano il contratto implementato.
L'utente ha scelto A. Coda e worker BSP sono collegati e verificati.
Anche il diagnostico AP1 -> coda -> BSP e implementato; i produttori AP
non sono ancora abilitati nella console ordinaria.

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

### Contratto della coda

- Sequenza: nucleo host, worker BSP, poi diagnostico AP dedicato, senza
  abilitare ancora i task AP nella console ordinaria. Modulo `kernel/console/video/`,
  relativi test centralizzati e script; nessun nuovo accesso hardware.
- Otto richieste, ciascuna con massimo 256 byte UTF-8 posseduti dalla coda;
  nessun puntatore del produttore viene conservato. Operazioni WRITE, EDIT
  e SUBMIT, con validazione completa prima della pubblicazione.
- Rust gestisce contenuto, indici, stati e validazione. Il gateway usa
  il lock kernel esistente e rifiuta chiamanti con altri lock detenuti.
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

### Gateway della coda: contratto di implementazione

`requests/queue.h` e l'interfaccia C; il nucleo e il gateway FFI sono in
`rust/requests/` e `rust/ffi/requests.rs`. Ogni sessione possiede un oggetto
C originale con lock e 2.560 byte di storage opaco, allineato a 8 byte.
La dimensione del nucleo e verificata a compilazione Rust; il layout dei
pacchetti e verificato su entrambi i lati. Nessuna allocazione o singleton.

Init richiede un oggetto azzerato, privato e non ancora pubblicato.
Vietati copia, spostamento o reinit di un oggetto vivo. Pubblicazione e
distacco sono responsabilita del binding della sessione: la memoria
non si libera finche esistono produttori, consumer o chiamate in corso.
CLOSED non autorizza da solo il rilascio mentre altri chiamanti lo usano.

Il gateway salva/disabilita IF con il backend sync esistente prima di
ottenere `kernel_lock_current_context()`: impedisce che una preemption renda
stantio il contesto prima dell'acquisizione. Il rango e 1 e ogni chiamata
richiede depth zero; non si ammette alcun lock esterno gia detenuto.
Acquisizione, deadline e ticket usano le primitive K2 senza modifiche.
L'accesso Rust finisce prima del release; un errore di release o un panic
Rust chiama il terminale `kernel_console_requests_fail_stop`, senza cleanup
o ripristino artificiale del lock. Il terminale nativo usa il fail-stop
dello scheduler nel collegamento BSP verificato.

I buffer di invio vengono copiati in un pacchetto locale prima del lock.
L'output e copiato dopo il release, prima del ripristino di IF. Errori di
take/query non modificano l'output. Si rifiutano dimensioni, allineamenti,
overflow e sovrapposizioni con owner o contesto corrente. I buffer devono
essere RAM kernel viva, privata del chiamante e di capacita dichiarata:
questi controlli non validano pagine arbitrarie e non sono user copy.
Solo il worker consumer chiama take/complete; nessuna API da IRQ/NMI.

Il simbolo raw Rust e un dettaglio privato: richiede storage inizializzato,
lock esclusivo e pacchetto distinto. I controlli numerici non provano
queste proprieta. Riferimenti Rust ufficiali:
[ptr::write, Safety](https://doc.rust-lang.org/core/ptr/fn.write.html#safety)
e [raw pointers](https://doc.rust-lang.org/core/primitive.pointer.html).
Il nucleo resta safe; l'unsafe e confinato a inizializzazione, prestito FFI
e terminale panic. Non si scrive alcun nuovo registro hardware.

### Collegamento nativo BSP del worker

Il backend `console/requests.c` collega la coda alla sessione video.
Input (task 0) e shell (task 2) accodano EDIT/SUBMIT e WRITE; soltanto
task 1 estrae e applica al servizio, prima del rendering. Mouse e chiamanti
nativi restano sul BSP: questo passaggio non abilita task driver sugli AP.

Si riusa l'evento worker esistente: 4 senza mouse, 6 con mouse. Il producer
osserva la generazione prima del push; FULL conserva il buffer sullo stack
e attende la generazione osservata, senza lock detenuti. Il consumer
segnala dopo ogni richiesta completata. Anche gli altri segnali dello
stesso evento richiedono di ricontrollare la coda. Massimo 1.024 tentativi
per invio, non per sessione; esaurimento o errore sono espliciti, senza
scartare silenziosamente il comando. Nessun polling attivo della coda.

Ogni turno applica fino a otto richieste, poi disegna il gruppo previsto.
Il SUBMIT viene accodato prima della pubblicazione nella FIFO comandi:
un'attesa per spazio non deve permettere alla shell di rispondere prima
della consegna della riga video. Sono preservati testo italiano e riga
in modifica mentre arrivano risposte ai comandi.

La chiusura richiede input hardware quieto, consumer input e shell conclusi,
coda vuota e nessuna applicazione in corso. Solo allora drain/close vietano
nuovi invii; il worker termina il disegno residuo e il teardown verifica
`KERNEL_CONSOLE_REQUESTS_CLEAN`. I produttori sono gia fermi prima del
distacco e della raccolta degli stack. Video assente conserva il percorso
seriale senza inizializzare la coda video.

Il collegamento Rust nativo usa una staticlib con LTO e panic abort per
includere le funzioni core necessarie senza dipendenze dal runtime Windows.
Il linker mantiene nodefaultlib: non si aggiungono stub di runtime per
nascondere simboli mancanti. Il terminale panic usa il fail-stop esistente.

### Primo produttore AP: diagnostico A approvato il 6 novembre 2026

L'ostacolo attuale e il binding dello scheduler, non il lock della coda.
`scheduler/mapping/native.c` mantiene un solo insieme collegato di stack
e contesti; `batch/runtime.c` lo usa per il diagnostico AP, mentre
`integration/kernel.c` lo collega al profilo ordinario della console.
Non si possono avviare i due percorsi insieme cambiando solo un controllo
CPU: uno potrebbe sovrascrivere le autorita di stack dell'altro.

Alternative:

- A, adottata: diagnostico dedicato AP1 -> coda -> BSP. Riusa il lotto
  di due task AP gia verificato, con BSP supervisore e consumer della sola
  griglia RAM. La console ordinaria e il rendering restano nel profilo
  attuale. Dimostra prima il passaggio reale fra CPU e il lifetime.
- B: integrazione direttamente nella console ordinaria. Richiede prima
  un unico binding dei task BSP/AP, ammissione concorrente, notifiche e
  risvegli tra CPU, e revisione del teardown input/video. Il perimetro e
  piu ampio; non e una semplice sostituzione del mittente.

Contratto implementato per A:

- Un descrittore del canale, inizializzato dal BSP prima dell'ammissione,
  pubblicato con release e acquisito dagli AP con acquire. Identita e
  token della sessione non cambiano durante il lotto. Nessun puntatore
  alla sessione PS/2, alla griglia o al renderer viene prestato ai task AP.
- Due task su AP1 inviano 1.000 WRITE ciascuno con identita e sequenza;
  la coda conserva otto posti da 256 byte e le copie gia verificate.
  EDIT e SUBMIT restano di proprieta dell'input BSP in questa prima prova.
- FULL conserva la richiesta e restituisce il task al suo dispatcher
  tramite lo yield AP esistente. Nessuna attesa sul dispatcher BSP, nessun
  nuovo IPI, IRQ o registro. Non dimostra ancora WAIT/HLT cross-CPU.
- Il BSP consuma al massimo otto richieste tra due controlli di progresso,
  applicandole al servizio C in RAM. Usa deadline e limite di controlli
  esistenti; contesa, attese e istruzioni hardware non cambiano contratto.
- Fine degli invii e quiescenza della CPU sono prove distinte. Il BSP
  aspetta fine di entrambi i produttori, quiescenza AP verificata e coda
  svuotata prima di close, revoca e raccolta degli stack. Un errore dopo
  l'avvio trattiene le risorse e termina il diagnostico.
- Rust gestisce identita/stati del canale e regole di ammissione; C
  collega risorse e contesti esistenti. L'Assembly di switch si riusa.
  Il primo blocco non estende automaticamente i permessi degli altri driver.

Perimetro approvato: console/video e soli adattatori del lotto/ammissione
AP, supervisione BSP, build e test. Nessun refactor generale dello
scheduler, del mapper o dei driver PS/2/VirtIO.

Accettazione: test host dei token e degli stati, thread concorrenti,
FULL e completamento incompleto; poi dieci boot consecutivi del nuovo
profilo con 2.000 WRITE esatte, ordine per produttore, CPU AP attestata,
riempimento forzato della coda prima del primo drain per provare FULL,
nessuna applicazione sul core produttore e budget fisico ripristinato.
Un negativo omette una conferma e deve negare la raccolta. Regressione
console BSP separata. L'integrazione ordinaria con wakeup SMP resta il
blocco successivo e richiede il proprio contratto.

Implementazione: `rust/requests/channel.rs` contiene lo stato atomico safe;
`rust/ffi/channel.rs` espone il confine numerico solo con cfg
`console_ap_diagnostic`. Token immutabile non nullo, conferme nei bit 0/1,
chiusura nel bit 2: stati ammessi 0, 1, 2, 3, poi 7. Identita dichiarate
verificate in Rust; il backend C verifica CPU e task reali prima dell'invio.
I bit sono metadati software, non registri hardware. Pubblicazione e
completamento usano release/acquire; token errati, duplicati e reinit
sono rifiutati. Nessun prestito alla griglia passa agli AP.

I profili `-TestApService ap-console` e `ap-console-incomplete` riusano
il lotto AP con due task. Il BSP applica le richieste in una griglia 40x8
senza disegno. Solo dopo due FULL osservati inizia il drain. Restano i
limiti del supervisore (1 secondo e 1.000.000 controlli per fase). Nei
retry FULL il produttore controlla un budget di 2 secondi dal proprio avvio
e ammette 65.536 tentativi per WRITE; il dispatcher ammette 65.536 passi.
Non sono garanzie temporali per hardware generico.

Nel negativo entrambi i task terminano e AP1 e fisicamente quiescente;
manca soltanto la conferma Rust del produttore 1. La chiusura e il guard
iniziale di `xat_cleanup` negano il rilascio, prima di qualsiasi COLLECT.
Le pagine restano pinned fino al panic SMP coordinato. Non e una prova
di recupero da AP bloccato. Non si aggiungono IPI, timer o Assembly.

L'audit dei simboli riconosce anche i membri Rust `archivio:*.rcgu.o`:
il diagnostico richiede i propri simboli, il profilo ordinario li vieta.
Il percorso C di produzione e i binding dei task ordinari non vengono
abilitati implicitamente dalla presenza del canale diagnostico.

Fonti: [Rust Ordering, Acquire e Release](https://doc.rust-lang.org/core/sync/atomic/enum.Ordering.html)
(documentazione consultata 1.99.1, implementazione compilata e verificata
con rustc 1.98.1); [Linux v6.12 workqueue, Introduction e The Design](https://docs.kernel.org/6.12/core-api/workqueue.html)
come riferimento per separare accodamento e consumer. Non si copiano
codice, worker pool o politica di scheduling Linux.

### Risveglio AP -> BSP: scelta A approvata

L'utente ha scelto l'IPI dedicato AP1 -> BSP. Il registro IRQ ora contiene
quattro subscription, con validatori, EOI e watchdog estesi a 0x43.
Il nucleo Rust della notifica e collaudato host, ma non e ancora collegato
al kernel. Restano da implementare trasporto ICR, handler e worker.

`xis_native_send` continua ad accettare solo mittente BSP e destinazione AP;
`xis_mmio_write` autorizza ancora soltanto il vettore 0xe1 del servizio.
Il percorso di eventi della console usa contesto e stack del task chiamante:
non basta chiamarlo da AP1 per modificare il dispatcher BSP.

Alternative:

- A, consigliata: notifica IPI dedicata AP1 -> BSP, vettore proposto 0x43,
  con subscription BSP originale e quarto slot esplicito. Il worker viene
  reso eseguibile attraverso il dispatcher; la coda rimane l'autorita sui
  dati. Gli invii possono raggruppare le notifiche gia pendenti.
- B: pending atomico consultato alla scadenza dell'attesa timer BSP.
  Richiede meno adattatori hardware, ma aggiunge latenza e controlli
  periodici. Il periodo va misurato nel profilo; non e un risveglio IPI.

Contratto approvato per A, integrazione hardware ancora da completare:

- Un solo canale AP1 -> BSP, token e descrittore stabili. WRITE viene
  prima copiato nella coda; solo dopo il rilascio del lock si pubblica
  la notifica. Rust valida token, stato, sequenze e pending; C possiede
  il mapping LAPIC e la subscription. Nessun pointer alla griglia negli AP.
- Pending raggruppato: solo la prima notifica non ancora presa richiede
  un IPI. Il protocollo deve gestire pubblicazione concorrente alla presa
  del pending. Le sequenze sono monotone, senza wrap o riuso live.
- L'handler BSP verifica la subscription, pubblica soltanto una notifica
  per il dispatcher e completa l'EOI. Non prende lock ordinari, non disegna
  e non chiama il percorso di evento di un task. L'Owner scheduler viene
  modificato dal dispatcher nel contesto verificato.
- Il pending va preso con un'operazione atomica prima di acquisire lo
  snapshot della sequenza pubblicata: un nuovo invio dopo la presa deve
  poter richiedere un altro IPI. Nessuna cancellazione separata dopo il
  rendering. Code non vuote vengono ricontrollate prima di ogni attesa.
- L'APIC puo raggruppare richieste dello stesso vettore: un EOI non equivale
  a un WRITE o a ciascuna scrittura ICR. L'ack software avanza alla sequenza
  osservata; contatori ISR/EOI descrivono solo le consegne reali.
- Stop: produttori quiescenti, invii terminati, sequenze osservate, pending
  e coda drenati, IRR/ISR pertinenti vuoti; poi distacco e revoca del token.
  Owner e callback restano vivi durante questa procedura. Timeout o errore
  dopo pubblicazione sono terminali e trattengono le risorse.
- Watchdog e istruzione HLT devono ammettere la nuova sorgente owned e
  conservare il controllo indipendente dei blocchi. Il risveglio BSP e
  la sospensione WAIT/HLT dei produttori AP sono contratti distinti: questa
  prima notifica non prova automaticamente il secondo.

Fonti hardware: [Intel SDM 253668-078US, Vol. 3A](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf),
§11.6.1/Figura 11-12 e §11.6.2.1: ICR low +0x300, high +0x310,
destinazione high[31:24], delivery low[10:8]=0, physical bit11=0,
delivery-status bit12, assert bit14, edge bit15=0. Destinazione: APIC ID
verificato del BSP, non indice CPU assunto uguale all'ID. §11.5.3:
ESR +0x280 con sequenza gia implementata dal backend ICR. §11.8.5:
EOI +0xb0, scrittura zero. Questi sono riferimenti per il progetto,
non nuove scritture hardware eseguite o validate.

Riferimento comparabile: [Linux v6.12, kernel/smp.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/kernel/smp.c),
accodamento del lavoro e invio IPI quando la lista cambia stato. Qui si
conservano coda limitata, owner BSP e failure terminali; niente copia del
codice Linux, callback arbitrarie o framework generico per tutti i driver.

Perimetro approvato: console/video, backend ICR/AP, registro e ritorno IRQ
BSP, adattatori di dispatcher/idle/watchdog e soli binding, build e test
necessari. L'unificazione dei binding BSP/AP resta un prerequisito per
usare i produttori AP nella console ordinaria; non si abilita aggirando
i controlli del mapper o condividendo dispatcher e stack tra CPU.

Accettazione: test Rust di pubblicazione/presa concorrenti, notifiche
raggruppate, token stantii, overflow e stop; test C di limiti e cleanup;
poi dieci boot consecutivi con vero risveglio del BSP da HLT, dati ordinati,
nessun evento perso e budget ripristinato. Negativi: invio/ack mancante e
stop con notifica in volo devono negare il rilascio. Regressioni dei tre
vettori esistenti e console. La forma del collegamento ordinario dipende
anche dalla verifica dei binding; non e dichiarata completa dal solo IPI.

### Nucleo delle notifiche e quarto slot IRQ

`rust/requests/notify.rs` e `no_std`, senza unsafe e senza allocazioni.
Contiene un token immutabile, sequenze pubblicate/osservate, pending atomico
e due record di invio, uno per produttore AP1. I parametri CPU identificano
il ruolo: il futuro adattatore nativo deve verificare la CPU reale, IF e
lifetime; il solo valore passato a Rust non dimostra questi prerequisiti.

LIVE accetta invii; SEALED li vieta ma permette di terminare quelli gia
prenotati. CLOSED richiede zero invii in corso, pending vuoto, sequenze
confermate, coda vuota e hardware quiescente attestati dal backend.
Overflow e guasti interni rendono lo stato terminale, senza riuso.
Un invio restituisce sequenza e bit che richiede l'IPI; il mittente deve
confermare esattamente quel record dopo l'invio, anche quando raggruppato.
Ogni ciclo CAS e limitato a 32 tentativi; il rifiuto e esplicito.

La presa azzera pending prima di leggere la sequenza. La chiusura acquisisce
prima SEALED con zero riferimenti e solo dopo controlla pending e sequenze:
impedisce di usare snapshot precedenti all'ultimo mittente. `query` e una
lettura diagnostica per campo, non uno snapshot coerente o un permesso di
rilascio. Nessuno stato Rust attesta da solo una scrittura ICR o un EOI.

Il registro conserva token generazionali, rifiuto di ID duplicati e handler
non posseduti. Il watchdog ammette il bit IRR del vettore 0x43 solo con
subscription valida; continua a rifiutare sorgenti sconosciute. L'EOI
richiede il solo ISR atteso e il readback vuoto. ISR120h/IRR220h bit3 e
EOI b0h sono descritti nel SDM 078US, §11.8.4/Fig11-20 e §11.8.5/Fig11-21.
Questo blocco non abilita una sorgente hardware o un nuovo produttore.

## Checkpoint storico: servizio BSP del 10 ottobre 2026

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
