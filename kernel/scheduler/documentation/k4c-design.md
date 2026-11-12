# K4-C — Scheduler SMP con preemption

Design approvato il 4 novembre 2026, aggiornato il 5 novembre. Chiude K4 insieme a
K4-A (IPI) e K4-B (shootdown). Decisione di base già presa: D3-A di
[registro delle decisioni](../../../docs/decisions.md), cioè una coda globale con un
lock e preemption dal timer LAPIC di ogni CPU, dietro un'interfaccia
`pick/enqueue` sostituibile.

## Decisioni (4 novembre 2026)

Scelte dall'utente: switch via dispatcher (2-A), guard RAII Rust sul
lock di `kernel/sync` (3-A), tutti i task su qualsiasi CPU (sezione 5).

## Punto di partenza storico, prima di K4-C

- Lo scheduler è cooperativo e gira solo sul BSP: 8 slot, un solo
  `current`, ogni task torna al dispatcher a ogni yield o exit.
- Lo stato vive in Rust (`kernel/scheduler/rust/state`), ma in un
  singleton protetto da un booleano `ENTERED` che assume BSP e IF=0.
  Il gateway dichiara esplicitamente che SMP e IRQ non sono supportati.
- Il timer LAPIC esiste solo sul BSP ed è usato a sessioni.
- Gli AP sono online, in HLT con IF=1, e servono solo IPI di PING e flush.
- Molti driver assumono "gira solo il BSP" (`ks_boot_cpu`).

## Obiettivo e accettazione

Task kernel eseguiti su 4 CPU, sospesi dal timer anche senza yield.
Accettazione da G7: stress con task su 4 CPU, nessuna corruzione,
audit G5 pulito. In più: nessun task perso o eseguito su due CPU,
budget fisico invariato, regressioni ordinarie e console verdi.

Fuori perimetro: user mode, priorità, code per CPU, bilanciamento,
hotplug, x2APIC, PCID.

## Ruolo di Rust

Rust diventa il proprietario di tutto lo stato dello scheduler e delle
decisioni. C e Assembly restano per ciò che tocca la CPU.

| Parte | Linguaggio | Motivo |
| --- | --- | --- |
| Stati dei task, coda globale, `current` per CPU, affinità | Rust | Invarianti verificabili, nessun `unsafe` nel core |
| Decisione di preemption e conteggio del timeslice | Rust | Funzione pura, testabile in host in modo esaustivo |
| Lock del core: guard RAII su `kernel/sync` | Rust + C | Il rilascio è garantito su ogni percorso dal `Drop` |
| Validazione dei frame di interrupt e del contesto salvato | Rust | Dati non fidati che arrivano dall'Assembly |
| Calibrazione e reload del timer per CPU | Rust (esiste già) + C | La validazione Rust del reload si riusa |
| Entry IRQ, salvataggio e ripristino del contesto, IRETQ | Assembly | Registri e stack |
| MMIO LAPIC, CR3, accesso per CPU via GS | C | Accesso hardware con fonti SDM |

Il core Rust resta `#![forbid(unsafe_code)]`. L'unico `unsafe` sta nel
gateway FFI, come oggi.

## Alternative

### 1. Dove avviene lo switch di preemption

- **A (consigliata):** il timer segna "da rischedulare" e lo switch
  avviene all'uscita dell'IRQ, in Assembly, verso il dispatcher della
  CPU. L'handler C non cambia mai stack.
- **B:** switch dentro l'handler C. Più semplice da scrivere, ma
  mescola l'annidamento dell'IRQ con il cambio di task. Scartata.

### 2. Modello di switch

- **A (consigliata per la v0):** si conserva il modello attuale: il task
  torna sempre al dispatcher della propria CPU, che sceglie il prossimo.
  Due switch per cambio, ma politica, cleanup e failure restano in un
  solo punto, come oggi.
- **B:** switch diretto task → task. Più veloce, da valutare con misure.

### 3. Lock della coda globale

- **A (consigliata):** un `KERNEL_LOCK` di `kernel/sync` (ticket, rank
  verificato) preso da un guard RAII Rust attorno a ogni operazione
  sull'`Owner`. Il core Rust lavora su `&mut Owner` senza sapere nulla
  di CPU.
- **B:** spinlock scritto in Rust nel crate. Duplicherebbe le regole di
  rank e di IF già validate in K2.

### 4. Quando un task può essere interrotto

Solo se il frame interrotto è un task (non il dispatcher né l'idle),
con IF=1 e nessun lock tenuto (`Locks.Depth == 0`). Altrimenti la
richiesta resta pendente e si applica al prossimo yield o all'uscita
dall'ultimo lock. Questa regola protegge il codice scritto per il
modello cooperativo.

### 5. Affinità

Decisione dell'utente: tutti i task possono girare su qualsiasi CPU.
Ogni task conserva una maschera di CPU, usata dalla coda globale e utile
per test e diagnostica, ma nel profilo ordinario vale "tutte le CPU".

Conseguenza: i driver usati dai task (console, PS/2, VirtIO, shell,
watchdog) oggi rifiutano o presuppongono l'esecuzione sul BSP. Prima
che i loro task migrino, ogni oggetto driver deve diventare sicuro su
più CPU con un lock per oggetto (rank in `kernel/sync`) oppure con un
owner unico e richieste via IPI, come previsto da G6/G7. Gli IRQ dei
dispositivi restano instradati verso il BSP in K4.

### 6. Timeslice

Ipotesi iniziale da misurare: 10 ms con timer LAPIC periodico. Il valore
è un parametro, non un benchmark. La misura del costo di switch e del
jitter fa parte della validazione.

## Sotto-task

1. **K4-C1, core Rust SMP:** `current` per CPU, affinità, timeslice e
   decisione di preemption; guard RAII sul lock. Test Rust esaustivi e
   stress C multi-thread sul vero oggetto Rust, come per la mailbox.
2. **K4-C2, timer per CPU:** timer LAPIC periodico su ogni AP con il
   reload calibrato dal BSP; vettore registrato una sola volta.
3. **K4-C3, entry di preemption:** salvataggio completo nel contesto del
   task, controllo Rust del frame, switch al dispatcher all'uscita
   dell'IRQ. Prima sul solo BSP.
4. **K4-C4, dispatcher sugli AP:** gli AP passano dall'idle di servizio
   al dispatcher, continuando a servire PING e flush. Prima con soli task
   di lavoro senza driver.
5. **K4-C5, driver sicuri su più CPU:** audit dei controlli
   `ks_boot_cpu` raggiungibili dai task, poi lock per oggetto (o owner
   unico) per console, PS/2, VirtIO, shell e watchdog. Solo dopo, tutti
   i task migrano.
6. **K4-C6, campagna di chiusura K4:** stress su 4 CPU, audit G5,
   accounting, regressioni, console VirtIO/VT-d. Poi push di K4.

## K4-C2 — Prima prova del timer sugli AP

Il BSP termina il collaudo one-shot e pubblica il reload calibrato
con il suo divisore. Gli AP sono già in idle di servizio. Il profilo diagnostico usa un comando esplicito della mailbox per armare e fermare il
timer locale. La richiesta porta una generazione; il parametro
calibrato è pubblicato dal BSP e rimane immutabile per la prova.
L'ACK certifica che l'AP ha verificato la configurazione locale.

Nella IDT di servizio si riserva 0xd1 al timer degli AP, in una
classe di priorità sotto l'IPI 0xe1. Il LVT Timer (+0x320) usa
vettore nei bit 7:0, maschera nel bit 16 e modalità periodica
nei bit 18:17 con valore 01b. Il divide register (+0x3e0) usa
il divisore della calibrazione BSP (codifica 3, divide per 16).
Si scrive il reload in Initial Count (+0x380) dopo il LVT.
Current Count (+0x390) è in sola lettura. Si verificano i valori
programmati e si scrive zero in Initial Count per lo stop.
Riferimento: Intel SDM 078US, Vol. 3A, §11.5.4, figure 11-8,
11-10 e 11-11; §11.8.5 per EOI (+0xb0).

Il primo handler verifica identità, stato per CPU e vettore in
servizio. Conta le consegne dopo EOI. Le interruzioni LAPIC possono
accorparsi: il contatore misura consegne, senza rappresentare
tutti i periodi trascorsi. La prova non cambia il dispatcher.
Prima di dichiarare il timer fermo si maschera il LVT, si azzera
Initial Count e si gestiscono eventuali IRQ già pendenti.
Un timeout porta al panic coordinato.

Accettazione: tick su ogni AP reale, PING e flush durante la prova,
stop verificato, nessun tick dopo lo stop, regressione del timer BSP
e dieci boot consecutivi a quattro CPU. Il riuso del reload BSP
va misurato rispetto al clock PM per ogni AP sul profilo QEMU xAPIC.

Decisione: primo collegamento nel profilo isolato `-TestApService ap-timer`.
La mailbox usa due bit per il comando e 62 per la generazione.

## K4-C3 — Frame e ripresa sul BSP

Il frame può restare nello stack pinned del task. L'entry IRQ salva
15 registri, allinea RSP a 16 byte, riserva 32 byte di shadow space
e 512 byte FXSAVE. Il frame con vettore, RIP, CS, RFLAGS, RSP e SS
occupa 48 byte. Sono dimensioni dell'ABI attuale di return.asm.
Per il frame hardware e IRETQ si applica Intel SDM Vol. 3A §6.14.

Prima di leggere il frame, C identifica lo stack e Rust verifica
che tutta l'area salvata vi rientri. Il controllo avviene dopo i
salvataggi Assembly. Le pagine di guardia restano la protezione
durante l'ingresso.

Nel profilo TestSchedulerPreemption la continuazione resta nello
stack del task. Il ramo Assembly dopo il ritorno dell'handler
la sospende usando lo switch esistente.
Il dispatcher applica la transizione Rust prima di rieseguire il
task. La ripresa torna all'epilogo IRQ, che ripristina FXSAVE,
i registri e il frame tramite IRETQ. Prima della sospensione si
verificano EOI, task e generazione correnti, IF originario e
assenza di lock. Lo stack resta posseduto fino al termine del task.

Alternativa: copiare il frame completo in un oggetto separato per
task. Richiede un nuovo formato di ripresa e un secondo percorso di
ripristino. La prima prova deve interrompere un task senza yield,
far avanzare un altro task e riprendere il primo con registri e
flags intatti. Dispatcher e idle devono proseguire senza switch
preemptivo quando ricevono il timer.

## Preemption periodica prima del dispatcher AP

Il diagnostico BSP ha provato un singolo switch con il timer one-shot.
Il diagnostico AP ha provato la sorgente periodica durante l'idle.
Il prossimo collegamento deve distinguere la consegna del tick
dalla decisione di cambiare task.

Decisione: profilo periodico isolato sul solo BSP, TestSchedulerPeriodic. Il profilo prende
in carico il LAPIC timer dopo la chiusura della sessione one-shot
esistente, con source mascherata e senza consegne pendenti. Due task
di calcolo senza yield alternano esecuzione fino a un limite
esplicito. Il timer completa EOI prima dell'epilogo IRQ. Il dispatcher
Rust consuma il tick e decide se riprendere il task o rimetterlo in
coda. Il dispatcher opera con IF disabilitato durante questa prova.
Lo stop al sesto IRQ verifica source ferma e ISR/IRR vuoti prima
del distacco; una sorgente ancora pendente fa fallire la prova.
Il teardown ripristina LVT, divisore e budget. I test confrontano progresso, contesto e
numero di switch su dieci boot QEMU.

Il passo AP richiede poi tre garanzie proprie: verificare l'indice
CPU dal contesto locale prima di usare il gateway Rust, separare
stack e stato dei dispatcher, accettare PING e flush anche quando
l'IRQ interrompe uno stack task autorizzato. Il gate attuale del
gateway verifica il dispatcher BSP; il parametro cpu da solo
non autorizza operazioni sullo stato di un altro core.

Alternativa: collegare subito il timer periodico e il dispatcher
su un AP nel medesimo diagnostico. Dimostra prima il lavoro su un
core secondario, ma unisce la validazione dello switch ripetuto,
dell'identità CPU e dei nuovi stack nello stesso collaudo.

## K4-C4 — Autorità del chiamante

Primo sottoblocco del 5 novembre 2026. Prima di ammettere dispatcher AP,
il gateway deve confrontare il parametro CPU con il contesto locale
verificato dal backend. Il controllo avviene sotto il lock del core e
prima di prendere riferimenti allo stato Rust. Il risultato del backend
è accettato soltanto se vale uno. Un rifiuto restituisce DENIED, lascia
intatto lo stato e rilascia il lock.

Il backend x86 riusa l'accessor per CPU e il gate del dispatcher esistenti.
Questo sottoblocco conserva il requisito corrente del dispatcher BSP.
L'ammissione degli stack dispatcher AP sarà una modifica successiva.
Le API con suffisso _on richiedono il core indicato. INIT, configure,
API legacy e drain IRQ restano autorizzati dal contesto della CPU zero.

Il collegamento è richiesto nel profilo scheduler_core_lock usato dalla
build kernel. I test host senza quel profilo conservano il loro modello
di contesto. Lo stress host con lock deve invece modellare un'identità
distinta per thread e passare dallo stesso gate della build kernel.

Accettazione di questo sottoblocco. Rifiuto di identità diversa, contesto
assente e risultato non normalizzato, su apply, eventi, stop, tick e
affinità. Rifiuto delle operazioni BSP da un thread AP. Prove positive
degli eventi sulle quattro identità, lock bilanciati e stress sul vero
oggetto Rust. Regressioni scheduler e dieci boot per profilo ordinario e
periodico BSP. Queste prove precedono il vero dispatcher AP.

## K4-C4 — Motore indirizzabile per CPU

Secondo sottoblocco del 5 novembre 2026. Il motore espone dispatch_on(cpu)
e inoltra selezione, yield, eventi, stop, raccolta e tick alle API _on.
L'ingresso legacy resta un adattatore per CPU zero. Il backend deve
confermare l'identità con risultato esattamente uno prima di accedere
alla guardia d'ingresso della CPU. Il gate viene ricontrollato dopo
run, idle e release_actor. Una guardia distinta per ciascuna delle 64
identità impedisce il rientro sul medesimo core; non sostituisce il lock
globale Rust. Il chiamante resta a IF=0, fuori da IRQ/NMI, sulla stessa
CPU per tutta la chiamata. Configurazione e statistiche sono possedute
dal chiamante e restano stabili.

Il drain delle notifiche IRQ rimane esclusivo della CPU zero: una
configurazione AP che lo richiede viene respinta senza mutare lo stato.
Il lotto resta esclusivo, con esattamente task_count task pubblicati:
la condizione finale EMPTY riguarda ancora la coda globale. Questo
blocco non autorizza lanci concorrenti di lotti né modifica stack,
contesti nativi e ammissione degli AP. La chiusura coordinata dei
dispatcher concorrenti richiederà il successivo collegamento AP.

Accettazione: ciclo completo sul vero Owner Rust con lock e identità
host da zero a 63, affinità singola, yield, observe/signal/wait con
generazione cambiata, stop/query, exit e raccolta. Provare identità
falsa, indici fuori limite, gate zero/non normalizzato, rientro, drain
AP vietato e rilascio della guardia dopo fallimento. CPU, stack e
risorse del nuovo test restano mock; il kernel continua a usare solo
l'adattatore BSP. Regressioni host e dieci boot ordinari e periodici.

## K4-C4 — Contesto nativo per CPU

Terzo sottoblocco del 5 novembre 2026. Il contesto Assembly del dispatcher,
fase, task corrente, generazione, richiesta pendente e statistiche passano
in uno slot per CPU. Gli slot sono statici, allineati a 64 byte e non
condividono linee di cache. Non richiedono nuove lease né allocazioni heap.
Gli otto contesti dei task e le risposte degli eventi restano indicizzati
per task; coda, proprietà logica e transizioni restano nel core Rust.

L'accessor usa l'identità per CPU già verificata. Rifiuta indice fuori
limite, CPU non attiva, self incoerente e owner diverso da quello della
prima preparazione. La preparazione riguarda soltanto il proprio slot,
prima del suo uso e con IF=0; una chiamata ripetuta conserva il contesto.
Non inizializzare da IRQ/NMI e non pubblicare puntatori agli altri core.
Gli aggiornamenti del singolo slot restano del suo core; non sostituiscono
il lock Rust o il futuro protocollo di ammissione.

Nel percorso dei task il puntatore locale viene riacquisito dopo yield,
richieste di eventi, sospensione IRQ e ritorno dall'entry del task.
Questo evita di trattenere il riferimento precedente attraverso uno
switch esplicito. La migrazione preemptiva fra CPU richiederà anche la
revisione delle sezioni native che usano dati per CPU con IF=1.

Il bootstrap dello scheduler conserva il requisito BSP. Budget, profilo
di test e binding del mapper restano nell'orchestrazione BSP; il gate
nativo continua a rifiutare gli AP. Nessun cambio al formato FXSAVE o
all'Assembly. C possiede questi oggetti ABI; Rust mantiene gli stati dei
task. Nessun wrapper C++ aggiunto in assenza di una responsabilità utile.

Accettazione: slot separati e allineati su 64 identità host, rifiuto di
rebind, dati conservati alla ripreparazione e quattro thread che aggiornano
esclusivamente il proprio contesto. Regressioni host e dieci boot per
profilo ordinario e periodico BSP, con risorse e marker verificati.
Queste prove non costituiscono ammissione o preemption dei task sugli AP.

## K4-C4 — Validazione del frame di servizio AP

Quarto sottoblocco del 5 novembre 2026. Prima di ammettere stack diversi
dall'idle, il servizio deve verificare tutta l'area salvata dall'entry
Assembly, non soltanto i 48 byte visibili al C. Il controllo numerico
riusa x86_irq_saved_area: 120 byte GPR, allineamento a 16, 32 byte shadow
space e 512 byte FXSAVE sotto il frame. Si applica prima di leggerne i
campi; non può annullare scritture già eseguite dall'entry. Guardie e
proprietà dello stack restano prerequisiti dell'ingresso hardware.

Una funzione Rust senza unsafe verifica poi il frame di ritorno idle:
RSP uguale a top meno 576, frame a align_down(RSP,16) meno 48, selettori
8/16 e maschera flags 0x7602. I valori ammessi restano 0x202 ordinario
e 0x602 diagnostico: bit 1 fisso e IF9, con DF10 solo nel test che ne
verifica il ripristino; IOPL12:13 e NT14 devono essere zero. Nessuna
nuova autorizzazione per stack task. Il wrapper FFI riceve soli valori,
senza riferimenti o letture di puntatori in Rust.

Minacce considerate: puntatore fuori stack prima della lettura C, area
FXSAVE oltre il limite e frame con posizione incoerente. Il rifiuto è
terminale e precede MMIO e ACK della mailbox. Non autentica da solo CPU,
mapping o proprietà: questi restano verificati dal backend. I task kernel
rimangono fidati e questa modifica non introduce una barriera ring 3.

Intel SDM 078US §6.14.2 documenta SS:RSP incondizionati e allineamento
prima dei cinque slot CPU. L'entry esistente aggiunge lo slot vettore:
sei slot da otto byte. §6.14.3 documenta il ripristino con IRETQ. La
dimensione FXSAVE e lo shadow space sono quelli dell'Assembly esistente.

Accettazione: matrice Rust per selettori, flags, profili e geometria,
debug/release e Clippy; vero collegamento C/Rust, rifiuto prima della
dereferenziazione quando frame o spill oltrepassano i limiti. Campagne
ordinarie, PING/flush con preservazione GPR/FPU/SIMD e timer AP, dieci
boot consecutivi per profilo. Gli stack dei task restano non ammessi.

## K4-C4 — Ammissione AP

5 novembre 2026. Scelta A confermata dall'utente. I quattro blocchi
verificati finora coprono identità del gateway, motore indirizzabile,
contesto nativo per CPU e frame idle. Nessun AP esegue ancora un task.

### Scelta del canale

**A, scelta:** stato di ammissione separato in Rust, risveglio tramite
il PING esistente. Non cambia codifica, generazioni o ACK della mailbox
PING/flush/timer. Richiede di verificare separatamente ACK del PING e
ammissione al dispatcher. Una sola richiesta di ammissione per CPU nel
profilo iniziale; nessuna coda generica di callback o puntatori eseguibili.

**B:** nuovo comando nella mailbox di servizio. I due bit operazione sono
già usati da PING, flush, arm e stop del timer: occorre cambiare la codifica
e ridurre il campo generazione. Un solo canale di trasporto, ma modifica
anche un protocollo già verificato da cui dipende il riuso delle pagine.
L'ACK hardware deve comunque restare distinto dalla fine del lavoro.

Entrambe le alternative fanno entrare nel dispatcher soltanto dopo il
ritorno dall'IRQ. Nessun cambio di contesto dentro l'handler C.

### Primo profilo e proprietà

Prima ammissione in un diagnostico dedicato, su AP1; gli altri AP mantengono
il servizio. Nel blocco di ammissione non si pubblicano task: si provano
ingresso fuori IRQ, contesto locale, conferma e ritorno all'idle. Nel
blocco successivo il BSP prepara un lotto di task di calcolo con affinità
AP1. È un vincolo del collaudo, non la politica definitiva delle affinità.
Estensione ai dispatcher concorrenti dopo la prima esecuzione verificata.

Il BSP possiede configurazione e risorse; le rende stabili prima della
pubblicazione. Rust possiede generazioni, destinatari e stati atomici.
C verifica identità, root, stack privato posseduto, descrittori, assenza
di lock e contesto locale. Assembly chiama il punto di ingresso soltanto
dal ciclo idle con IF=0 e DF=0, non dall'epilogo IRQ. Il formato del frame
resta invariato. Nessun callback arbitrario passato nella richiesta.

Protocollo del primo collaudo, pseudocodice non eseguibile:

```text
BSP: prepara risorse -> pubblica OFFERED con release -> invia PING
AP:  acquire OFFERED -> claim esclusivo -> verifica contesto nativo
     -> pubblica ADMITTED con release -> ritorna all'idle di servizio
BSP: verifica ACK PING e ADMITTED con acquire, entro deadline
```

Indice CPU, identità del gruppo e generazione devono coincidere. Un
secondo claim, token vecchio o richiesta da CPU diversa vengono rifiutati.
FAILED è terminale. Non si riutilizza una generazione e non si azzerano
stati mentre un AP può ancora osservarli. Nessun borrow Rust o lock può
attraversare ingresso, IRQ o cambio di contesto.

Dopo la pubblicazione un timeout o errore nativo provoca panic coordinato,
con risorse trattenute. Non è una cancellazione seguita da rilascio. Il
successivo protocollo di esecuzione dovrà aggiungere conferma di quiescenza
prima della raccolta degli stack; ADMITTED da solo non autorizza cleanup.

### Risvegli e limiti

La richiesta è pubblicata prima del PING. Il ciclo idle controlla il
lavoro con IF=0 prima della sequenza contigua STI/HLT. Se il PING arriva
fra controllo e sonno, resta pendente e interrompe HLT; se il lavoro era
già visibile, il punto di ingresso può consumarlo. Il controllo si ripete
anche dopo ogni risveglio. La scadenza usa il timer PM esistente, con
limite di poll. Nessuna attesa infinita e nessun riavvio dopo FAILED.

Il primo diagnostico usa TestApService admission e non abilita timer del dispatcher, migrazione o
accesso ai driver dagli AP. L'autorizzazione dei frame task e la verifica
del mapping restano gate del blocco di prima esecuzione, non conseguenze
automatiche di ADMITTED.

### Accettazione e ricerca

Test Rust di transizioni, token/CPU errati, claim concorrenti, timeout
contro conferma e fallimento terminale; stress C sul vero oggetto Rust.
Fixture C/Assembly per ingresso fuori IRQ e ritorno ABI. Dieci boot del
diagnostico con AP1 realmente ammesso e PING/flush funzionanti prima e
dopo; regressioni ordinarie e timer AP. Nessun PASS basato solo su mock.

Riferimenti consultati: Linux v6.12 kernel/smp.c separa pubblicazione del
lavoro e invio dell'IPI; riferimento concettuale, senza copiare codice o
introdurre il suo sistema generale di callback. Intel SDM 078US §6.8.1
per IF/STI e finestra del risveglio, §6.14 per ritorno dagli IRQ e §11.6.1
per il trasporto IPI esistente. La scelta A ha preceduto il nuovo codice.

Nel primo collegamento il gruppo è identificato dalla root verificata,
il token è uno e non esiste riarmo. Gli stati sono COLD=0, OFFERED=1,
CLAIMED=2, ADMITTED=3, FAILED=4; token a 61 bit e stato nei tre bit bassi.
La codifica è separata dalla mailbox IPI a due bit, che resta invariata.
L'entry statica è chiamata dopo IRETQ, verifica dei registri e CLD del
diagnostico: inizializza il solo slot nativo AP1 e conferma ADMITTED.
Il record pubblicato include root, token, identità, stack e puntatore
locale; il runner controlla anche i 704 byte dello slot locale e i
registri reali dopo il ritorno all'idle. La fase nativa rimane zero:
il motore scheduler e i frame task non sono ancora autorizzati.

## K4-C4 — Primo lotto AP

Primo profilo su AP1, due task di calcolo con affinità fissa. Il BSP
prepara stack, contesti e coda Rust prima del via; i task usano yield,
poi exit. Niente migrazione, driver o timer di preemption in questo
blocco. Gli IRQ PING/flush devono restare utilizzabili durante i task,
con frame autorizzati dallo stack e dalla generazione correnti.

**Scelta A, confermata dall'utente:** rilascio sul BSP dopo quiescenza. Il dispatcher AP
mantiene gli slot FINISHED e le lease vive; non esegue COLLECT. Il via
e la conclusione usano stati separati nel protocollo Rust. La conferma
QUIESCED viene pubblicata soltanto dopo il ritorno dal dispatcher,
con current assente, nessun lock e nessun accesso futuro agli stack task.
Il BSP verifica FINISHED, revoca le pagine, attende il flush di tutti
i core e soltanto dopo esegue COLLECT. Prima della quiescenza non si
riusano slot, contesti o frame. Timeout: panic e risorse trattenute.

**Alternativa B:** raccolta direttamente dall'AP. Richiede un servizio
di shootdown iniziabile dagli AP, coordinamento con il BSP e una revisione
più ampia del mapper. Non basta abilitare COLLECT sul core secondario.

Il motore mantiene il comportamento attuale come default. Un percorso
esplicito può lasciare FINISHED senza liberarli, preservando il contratto
Rust che richiede cleanup nativo prima di COLLECT. Il gate CPU distingue
BSP e AP senza richiedere APIC_BASE.BSP sugli AP; stack dispatcher e
stack task mantengono autorità e controlli differenti. Il gate del mapper
per mutazioni resta BSP. Nessun riferimento Rust attraversa lo switch.

Accettazione: casi host per FINISHED trattenuti, rifiuto di raccolta
prematura e completamento/timeout concorrenti; due task reali su AP1,
yield/exit e contesto preservato. PING/flush su stack task, ritorno del
budget dopo quiescenza e raccolta BSP. Dieci boot del profilo e regressioni
ordinarie, ammissione e timer AP. La scelta A/B precede il codice.

Il profilo TestApService ap-batch esegue due task su AP1: sedici yield
ciascuno, checksum intero e accumulatore floating point, poi exit.
La finestra con IF=1 usa il backend esistente e non tiene lock. Il BSP
attende progresso di entrambi, invia PING/flush durante il lavoro e
autorizza la conclusione. Il ritorno del task non dipende dall'ACK idle:
durante il lavoro si attende l'ACK dopo EOI; il controllo dei registri
idle si verifica soltanto dopo il ritorno al ciclo di servizio.

Stati aggiunti: START=5 pubblicato dal BSP, RUNNING=6 acquisito da AP1,
QUIESCED=7 pubblicato dall'AP dopo il ritorno del dispatcher. FAILED=4
resta terminale. Il token massimo è riservato per evitare che QUIESCED
coincida con INVALID. Il permesso di raccolta è verificato in Rust:
caller BSP, gruppo/token corrispondenti e QUIESCED. Il segnale di fine
lavoro accetta anche una quiescenza già avvenuta dopo il suo invio:
una conclusione rapida non diventa un errore spurio.

Il motore retained lascia FINISHED senza COLLECT; l'API ordinaria mantiene
la raccolta immediata. Le risorse preparate prima della pubblicazione
dei task possono essere revocate con ABORT; dopo il via gli errori sono
terminali e non autorizzano cleanup. La raccolta BSP revoca entrambi gli
stack, attende gli shootdown e poi esegue COLLECT. Si verifica il budget
intero prima, durante la ritenzione e dopo il rilascio.

Il gate AP usa una fotografia dell'arena acquisita dal BSP e trattenuta
per l'esecuzione. Controlla identità, root, GDT/IDT, parent e foglie dello
stack AP, guardie e ownership. Non acquisisce il lock fisico mentre il
lock scheduler è tenuto; il pinning viene verificato fuori dal lock e
la durata è garantita dalla ritenzione delle risorse. Le mutazioni del
mapper restano BSP. Intel SDM 078US §11.4.4: APIC_BASE.BSP8 deve essere
zero sugli AP; non viene simulato il bit BSP. Gli altri prerequisiti
CPU restano quelli del percorso esistente.

L'IRQ task verifica prima l'intera area salvata, poi geometria, selettori
e flags in Rust; C lega il frame allo stack e alla generazione correnti
e verifica RIP RX. Non cambia stack nell'handler. Context switch e
FXSAVE restano il backend Assembly esistente. Il segnaposto MSVC _fltused
è definito nel solo profilo sperimentale, senza collegare il runtime CRT.
Il test non introduce migrazione, preemption o accesso ai driver dall'AP.

## K4-C4 — Preemption AP1

Contratto del 5 novembre 2026: scelta A approvata dall'utente.
Il lotto cooperativo e il timer AP restano disponibili in profili distinti.
Il diagnostico ap-preempt li unisce senza promuovere il boot ordinario né i driver.

Due task su AP1 lavorano senza yield durante la fase misurata. Il timer
locale usa il vettore 0xd1 già riservato e il reload calibrato esistente.
L'handler verifica stack, generazione e sorgente, completa EOI e prepara
la sospensione. Il passaggio al dispatcher avviene dall'epilogo Assembly,
prima di FXRSTOR e IRETQ; il frame resta nello stack pinned del task.
Il dispatcher applica tick/preempt Rust per CPU 1, poi riprende la
continuazione salvata. Nessuna mutazione dello scheduler nell'handler C.

Il backend AP attuale ripristina direttamente il frame: occorre un hook
di uscita dedicato, senza riusare i singleton e le autorità BSP del bridge.
Preemption vietata con lock detenuti, IF precedente spento o contesto non
task; PING/flush e IRQ spuri non consumano il timeslice. Durante il lavoro
PING e flush restano ricevibili. Lo stato C dell'interruzione non mantiene
riferimenti Rust attraverso lo switch.

Scelta per il primo collaudo:

- A, approvata: AP1 conclude autonomamente la prova dopo tre sospensioni e riprese
  verificate per task. Ferma il timer locale prima della conclusione;
  ISR/IRR pendenti devono essere drenati e controllati prima di QUIESCED.
  Il BSP resta supervisore con deadline e raccoglie soltanto dopo la
  quiescenza, come nel contratto approvato.
- B: il BSP ordina la fine della prova mentre il timer resta attivo.
  Richiede distinguere stop richiesto, timer fermo, continuazioni sospese
  e loro completamento. Offre prima una prova di stop asincrono, con
  maggiore superficie di protocollo.

A isola prima la correttezza della ripresa. Sei
sospensioni sono un obiettivo del test, non sei periodi temporali garantiti:
gli IRQ possono accorparsi. L'assenza di yield è verificata dal workload;
registri generali, FPU/SIMD e avanzamento di entrambi sono prove richieste.

Pseudocodice non eseguibile: prepara sul BSP → avvia AP1 → task con IF=1
→ timer/EOI → epilogo/sospensione → dispatcher/Rust → ripresa/IRETQ
→ timer fermo e nessuna continuazione sospesa → QUIESCED → cleanup BSP.

Perimetro approvato: scheduler/batch, hook IRQ AP del servizio per-CPU,
relativi adattatori build e test. Il timer BSP espone soltanto il getter
del reload calibrato nel nuovo profilo; inizializzazione e watchdog non cambiano.
Accettazione: test host frame/transizioni/autorità e failure, dieci boot
del diagnostico, regressioni lotto cooperativo, timer AP, preemption BSP,
IPI e ordinario; budget fisico e mapping ripristinati. Su timeout o frame
invalido: panic coordinato, risorse trattenute. Coverage non presunta.

Fonti: Intel SDM 253668-078US §6.14 per frame/IRETQ, §11.5.4 e figure
11-8/10/11 per LVT (+0x320, maschera16, periodico17), divide (+0x3e0,
codifica3), initial/current count (+0x380/+0x390), §11.8.5 per EOI
(+0xb0). Initial Count=0 ferma il conteggio, ma non prova che un IRQ
precedentemente pendente sia stato consumato. Link nelle fonti sotto.

## K4-C4 — Dispatcher concorrenti: contratto approvato

Scelta A confermata dall'utente il 5 novembre 2026.
La scelta seguente riguarda il primo diagnostico concorrente; non modifica
la decisione finale di permettere task su tutte le CPU dopo C5.

**A, approvata:** AP1 e AP2 eseguono insieme due task ciascuno, con
affinità fissa. Il BSP prepara quattro stack, pubblica il lotto e resta
libero per supervisione, PING e shootdown. AP3 resta nel servizio idle.
Ogni AP usa contesto dispatcher, statistiche, record IRQ e timer propri.
La coda e il lock Rust restano globali: non si creano scheduler separati.

**B:** dispatcher concorrenti su BSP e AP1. Verifica prima la combinazione
dei due backend IRQ, ma il BSP deve alternare task e supervisione del
lotto. Richiede adattare anche il coordinamento oggi esterno al dispatcher.

### Ownership, completamento e failure

Il BSP possiede i quattro stack e i contesti. Li prepara e verifica tutti
prima di pubblicare il via; i descrittori condivisi diventano immutabili.
Ogni task appartiene a un solo AP nel diagnostico. I dati scritti da una
CPU e letti da un'altra richiedono pubblicazione release/acquire tramite
il backend atomico esistente; volatile non autorizza la comunicazione.
Nessun lock o riferimento Rust attraversa un cambio di contesto.

Un record Rust del lotto, protetto dal lock del core, lega gruppo, token,
CPU partecipanti e task attesi. Ogni AP conferma ingresso e completamento
del proprio lavoro. I task restano FINISHED; il BSP raccoglie soltanto dopo
QUIESCED di entrambi gli AP, timer fermo, current assente e nessuna
continuazione sospesa. La barriera di lotto non sostituisce tali controlli.

Il motore attuale termina con un conteggio locale seguito da SELECT=EMPTY:
questo è un contratto di lotto esclusivo, non una prova di completamento
globale. Il nuovo percorso deve distinguere assenza di task eleggibili,
lavoro ancora attivo altrove e lotto globalmente concluso. Il percorso
esclusivo esistente conserva il suo contratto. Nessun completamento viene
dedotto da una singola coda vuota o dal solo conteggio della CPU corrente.

Attese con timer PM e limite di controlli, senza tenere lock. Prima del
via è possibile rollback; dopo il via un timeout, frame invalido o token
incoerente causa panic coordinato con risorse trattenute. Nessun rilascio
su conferma parziale. La revoca dei mapping rimane sul BSP con shootdown.

Pseudocodice non eseguibile: prepara quattro task → ammetti AP1/AP2 →
pubblica lotto → entrambi confermano ingresso nel workload → esecuzione
concorrente con preemption → timer locali fermi → due QUIESCED →
verifica globale → revoca, shootdown e raccolta sul BSP.

### Prove richieste e perimetro proposto

Una barriera iniziale nel workload deve impedire che un AP finisca prima
che l'altro sia entrato. La prova attesta sovrapposizione delle sessioni,
non esecuzione fisica simultanea dei vCPU sotto TCG. Occorrono inoltre
progresso per entrambi, identità CPU reali, nessun task selezionato da due
CPU, preemption e ripresa per ogni task, preservazione GPR/FPU/SIMD,
PING/flush durante il lavoro e budget/mapping ripristinati alla fine.

Test Rust per barriera, token vecchi, conferme duplicate, CPU estranea e
completamento parziale; fixture C concorrente sul vero core Rust e ASan.
Dieci boot positivi del nuovo diagnostico e regressioni AP1, BSP e IPI;
negativi per rollback prima del via e mancata quiescenza, distinguendo
fault attesi da crash. Stress di chiusura C4 resta un obiettivo successivo.

Perimetro approvato: scheduler e backend x86 dello scheduler,
ammissione/hook IRQ AP nel servizio per-CPU, soli adattatori build e test
necessari. Nessuna migrazione dei driver, nuovo dispositivo o nuovo
vettore IRQ. Si riusano i registri LAPIC già documentati nella sezione
precedente, con autorità verificata separatamente per ogni CPU.

Ricerca: xv6-riscv, kernel/proc.c, funzione scheduler, come riferimento
per dispatcher locali e selezione protetta; Linux v6.12, kernel/smp.c,
per pubblicazione del lavoro e notifica IPI separate. Fonti sotto,
consultate il 5 novembre 2026. Sono confronti concettuali: nessun codice
copiato e nessuna adozione del loro protocollo di lock attraverso lo switch.

### Primo blocco: protocollo Rust e motore, prima del collegamento AP

Il core conserva il lotto dentro Owner, sotto lo stesso lock RAII della
coda. OPEN accetta esattamente due CPU secondarie con due task READY a
testa, ciascuno ad affinità singola. Tutti gli altri slot devono essere
liberi o ritirati; nessun task può essere già RUNNING. Group non nullo e
token crescente identificano il lotto. Non si riusano token dopo CLOSE.

Interfaccia in kernel/scheduler/lot.h: OPEN=0, ENTER=1, COMPLETE=2,
QUIESCE=3, QUERY=4, FAIL=5, CLOSE=6. Solo OPEN riceve la maschera dei
partecipanti; gli altri comandi richiedono participants=0. Il risultato
FFI è di 56 byte, con status e failed normalizzati, identità e tre
maschere. Gli errori azzerano tutti i dati oltre allo status.

ENTER autorizza SELECT sulla CPU partecipante. COMPLETE richiede i due
task FINISHED e current assente; QUIESCE richiede prima COMPLETE.
QUIESCE è una dichiarazione del backend fidato: Rust non osserva timer,
stack o registri. Il collegamento nativo dovrà verificarli prima della
chiamata. QUERY non cambia stato; CPU estranee e token errati sono respinti.

COLLECT resta negato anche al BSP finché tutte le CPU non sono QUIESCED.
Dopo tale barriera solo il BSP può raccogliere; CLOSE richiede tutti gli
slot del lotto FREE. RESERVE, PUBLISH e riconfigurazione restano bloccati
fino a CLOSE. Un fallimento congela il lotto e impedisce ulteriori SELECT
e raccolte; non cancella il current eventualmente rimasto dopo un errore.

x86_scheduler_dispatch_lot_on è un percorso retained distinto: ENTER
prima delle selezioni, conteggio locale di due completamenti, COMPLETE
prima del ritorno. Non richiede SELECT=EMPTY globale e non pubblica
QUIESCE. Su errore dopo l'ingresso tenta FAIL, poi restituisce fallimento
senza cleanup implicito. I chiamanti nativi devono trattarlo come terminale.

Questo primo blocco precedeva il collegamento AP1/AP2, descritto sotto.
Le fixture host usano il vero motore C e il vero Owner Rust,
ma simulano autorità CPU, stack e interrupt; non certificano il percorso
Assembly o una quiescenza hardware. Le regressioni QEMU esistenti misurano
soltanto l'assenza di regressioni dei profili già collegati.

### Collegamento nativo AP1/AP2

Il profilo TestApService ap-concurrent prepara quattro stack sul BSP,
assegna i task 0/1 ad AP1 e 2/3 ad AP2 e apre un solo lotto Rust.
Pubblica entrambi i via prima delle notifiche PING. Ogni AP entra nel
proprio dispatcher, continuando a ricevere PING e flush sui task.

Prima di armare il timer, il primo task di ciascun AP pubblica la propria
presenza e attende quella dell'altro con acquire/release, scadenza PM e
limite di controlli. Nessun lock viene trattenuto. Entrambe le sessioni
devono quindi essere entrate nel workload prima che una possa terminare;
non si afferma esecuzione fisica simultanea delle istruzioni sotto TCG.
La barriera iniziale lavora con IF=0, prima del timer e delle richieste
di flush durante il lavoro; non è un servizio di attesa per i driver.

Ogni CPU possiede record IRQ, stato del timer e statistiche distinti.
Ogni task possiede stack, contesto e contatori di completamento propri.
I descrittori condivisi sono immutabili durante il lotto; il BSP legge
i risultati ordinari soltanto dopo la quiescenza pubblicata dall'AP.
Le letture di progresso durante il lavoro usano il backend atomico.

Il gate verifica identità CPU, GDT e stack dell'AP corrente; una CPU non
può autorizzare lo stack task dell'altra. Rust conserva la stessa
affinità e vieta la selezione doppia. Sei sospensioni per AP, tre per
task, riusano l'epilogo Assembly già collaudato: dodici sospensioni in
totale. Non si introducono vettori o accessi LAPIC diversi dai precedenti.

Al ritorno il dispatcher verifica current assente, timer fermo, riprese
concluse e ISR/IRR drenati, poi pubblica QUIESCE nel lotto Rust e infine
QUIESCED nell'ammissione AP. Il BSP richiede entrambe le conferme per
entrambi gli AP prima di revocare i quattro stack, attendere gli
shootdown, raccogliere e chiudere il lotto. Il budget atteso è di 64
pagine task, tutte restituite. Timeout o errori mantengono il fail-stop.

Il diagnostico richiede almeno due AP; il collaudo corrente usa quattro
CPU totali. Il record task è di 384 byte nel solo nuovo profilo, con
padding esplicito; i vecchi profili mantengono 256 byte. Record separati
per AP e snapshot finale del lotto consentono al runner di verificare
identità, stack dei frame, conteggi e raccolta. La quiescenza nativa non
è dedotta dalla sola coda vuota o dal solo comando Rust.

### Stress circoscritto e conferma mancante

Due ulteriori diagnostici riusano il protocollo approvato, senza nuove
transizioni o accessi hardware. ap-concurrent-stress richiede otto
sospensioni/riprese per task e otto giri PING/shootdown prima del permesso
di conclusione: 32 sospensioni e 65 richieste IPI su quattro CPU totali.
Solo AP1/AP2 eseguono i quattro task. Le scadenze del poll Assembly,
del BSP e delle attese task restano invariate. Non è un benchmark di latenza.

ap-concurrent-incomplete omette soltanto QUIESCE Rust di AP2 dopo averne
verificato la quiescenza fisica. Entrambi gli AP pubblicano invece la
conferma di ammissione. Il BSP deve vedere completed=6, quiesced=2,
rifiutare COLLECT e mantenere le 64 pagine pinned. Il cleanup ordinario
deve poi provocare il panic coordinato prima del primo unmap. Il runner
controlla PTE, guardie, lease vive e pin, oltre alla causa e agli ACK NMI.
Non si simula un AP bloccato o ancora in esecuzione: è un test della
doppia conferma obbligatoria. L'iniezione è limitata al relativo profilo.

### Quattro CPU di lavoro: contratto approvato

Scelta A approvata dall'utente il 5 novembre 2026. Il punto di partenza
accettava esattamente due CPU secondarie; l'estensione ammette da due a
quattro AP, con due task ciascuno e raccolta sul BSP.

- A, approvata: quattro AP di lavoro e BSP supervisore, cinque vCPU
  totali. Otto task, due per AP, usano tutti gli slot attuali e 128 pagine
  di stack (512 KiB). Conserva un solo backend IRQ per i task AP e il
  cleanup sul BSP; non porta i driver sugli AP.
- B: quattro vCPU totali, con BSP e tre AP di lavoro. Richiede includere
  il BSP nel protocollo del lotto e spostarne la supervisione fra i
  callback del dispatcher, integrando anche il suo backend di preemption.

Per A: Owner ammette da due a quattro AP, mantiene due task ad affinità
singola per partecipante e rifiuta maschere fuori dalle CPU configurate.
Record e barriera diventano per partecipante; quiescenza completa prima
di qualsiasi raccolta. Lo stato Rust e i contesti restano separati; non
si aggiungono migrazione o riarmo del lotto. Un solo lotto per boot.

Perimetro approvato: scheduler Rust e backend x86, ammissione/hook AP,
soli adattatori build/test necessari. Riusa risorse per-CPU e protocollo
IPI esistenti; nessun nuovo dispositivo. Accettazione: test delle nuove
maschere e dei limiti, stress nativo su quattro CPU effettive, negativo
di conferma mancante con 128 pagine trattenute, dieci boot consecutivi e
regressioni dei profili precedenti. I profili sono ap-four e
ap-four-incomplete, entrambi con otto riprese per task e otto giri IPI.
Nel positivo si verificano 64 sospensioni/riprese e 116 richieste IPI;
nel negativo AP4 omette la conferma Rust: completed=30, quiesced=14 e
128 pagine ancora pinned al panic. Risultati nel documento di validazione.

## Lavoro restante dopo C4

C4 è completata per i diagnostici di calcolo fidato: ammissione,
preemption e dispatcher concorrenti su quattro AP, con raccolta BSP,
regressioni e negativi di conferma mancante. I risultati non promuovono
lo scheduler ordinario né i driver alla migrazione fra CPU.

Restano C5 (audit dei driver e lock per oggetto oppure owner unico) e
C6 (audit G5 globale, integrazione, stress finale e console VirtIO/VT-d).
Il rollback pre-avvio già collaudato resta un errore iniettato prima
della seconda allocazione, non esaurimento reale della RAM.
K5 userspace e K6 caricamento dei programmi sono milestone successive.

## K4-C5 — Binding comune BSP/AP

Scelta A dell'utente, 6 novembre 2026: un solo binding per tutti i task,
non un ponte separato per il BSP.

- **Storage unico:** `x86_task_stacks`, `x86_task_contexts`, risposte agli
  eventi e maschera FPU stanno in `integration/kernel.c`. Il batch AP non
  ha più copie proprie.
- **CPU e ingresso per task:** `x86_scheduler_task_assign(index, cpu, entry)`
  fissa la CPU e la funzione d'ingresso prima di acquisire lo stack.
  `x86_scheduler_task_start` chiama l'ingresso del task, poi
  `x86_scheduler_exit`.
- **Controlli per CPU:** `x86_scheduler_task_valid` rifiuta un task fuori
  dalla sua CPU. Il dispatcher del BSP usa lo stack principale, quello
  dell'AP `X86_AP_STACK_BASE(cpu)`.
- **Un solo percorso di switch:** yield, eventi, preemption ed exit passano
  dalle stesse funzioni su ogni CPU. Il batch usa i callback comuni
  `x86_scheduler_dispatch_run/reply/release`; resta suo solo il collect
  trattenuto.
- **SELECT per affinità (Rust):** `select_on(cpu)` risponde BLOCKED solo
  se esiste un task vivo eseguibile su quella CPU, altrimenti EMPTY.
  Prima bastava un task vivo su qualsiasi CPU: un dispatcher BSP non
  poteva finire mentre i task di AP1 erano ancora vivi.

Resta BSP-only la gestione delle mappature degli stack (acquire, release,
shootdown): gli AP le leggono soltanto.

## K4-C5 — Worker BSP risvegliato da WAIT/HLT

Scelta A dell'utente: watchdog invariato. L'attesa resta HLT fino alla
scadenza HPET di 5 ms; l'IPI 0x43 sveglia la CPU, il worker diventa
eseguibile dopo la scadenza.

- Nel diagnostico `ap-console-ipi` il consumo a polling è sostituito da un
  task worker sul BSP (indice 2, affinità CPU 0). Il worker fa OBSERVE e
  WAIT sull'evento 7 e svuota la coda.
- L'idle del dispatcher BSP azzera `xcn_wake`, chiama
  `x86_watchdog_idle(xcn_wake)` e poi il drain IRQ. L'handler 0x43 mette
  `xcn_wake[0] = 1`: un'attesa chiusa con EVENT prova un HLT interrotto
  dall'IPI.
- Il watchdog si apre prima di mappare gli stack del batch e si chiude dopo
  il loro rilascio. Gli stack dei task sono dichiarati come prestiti
  (`x86_watchdog_budget_begin_four`) durante il servizio.
- Il watchdog accetta ora una nuova apertura solo dopo una chiusura pulita,
  senza errori di sessione o di cleanup. Prima era una sola sessione per
  boot.
- Il limite di blocco dei produttori sale da 2 a 4 s solo in questo profilo:
  ogni attesa può durare 5 ms.

## K4-C5 — Comando AP nella sessione ordinaria

`ap <n>` invia un solo job alla volta ad AP1. La shell resta sul BSP e
attende l'evento 7. AP1 restituisce CPU, somma e checksum. Il dispatcher
BSP aspetta la quiescenza, raccoglie lo stack e pubblica il risultato o
l'errore. Su una sola CPU la sessione resta operativa senza servizio AP.

Il vettore 0x43 può arrivare anche durante la finestra IRQ del worker PS/2.
Il profilo AP lo ammette dopo la verifica dello stack, del task e del RIP.
Il registro IRQ verifica il binding e l'handler conferma la consegna.

Su `exit` il BSP chiude il canale dopo la quiescenza dei produttori e
prima di lasciare il dispatcher. In questa fase può ancora consumare gli
eventi pendenti. Un canale mai usato può chiudersi con zero consegne.

## Fonti

- [Intel SDM Vol. 3A, rev. 078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
  §6.8.1 STI/HLT, §6.14 entry e IRETQ a 64 bit, §11.5.4 timer LAPIC
  (modalità periodica, divisore, LVT), §11.8.5 EOI.
- [Linux v6.12, kernel/sched/core.c](https://github.com/torvalds/linux/blob/v6.12/kernel/sched/core.c):
  riferimento per `need_resched` e preemption all'uscita dell'IRQ.
  Nessun codice copiato.
- [Linux v6.12, kernel/smp.c](https://github.com/torvalds/linux/blob/v6.12/kernel/smp.c):
  pubblicazione delle richieste e notifica IPI.
- [xv6-riscv, kernel/proc.c](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/proc.c):
  scheduler per CPU con lock globale e switch verso il dispatcher.
