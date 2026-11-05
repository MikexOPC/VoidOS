# K4-C — Scheduler SMP con preemption

4 novembre 2026. Documento di design, senza codice. Chiude K4 insieme a
K4-A (IPI) e K4-B (shootdown). Decisione di base già presa: D3-A di
[G7](../../../docs/design/g7-smp-user.md), cioè una coda globale con un
lock e preemption dal timer LAPIC di ogni CPU, dietro un'interfaccia
`pick/enqueue` sostituibile.

## Decisioni (4 novembre 2026)

Scelte dall'utente: switch via dispatcher (2-A), guard RAII Rust sul
lock di `kernel/sync` (3-A), tutti i task su qualsiasi CPU (sezione 5).

## Punto di partenza

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

## Lavoro restante dopo i prerequisiti C4

Stima di pianificazione, non risultati né percentuale di completamento:

Ammissione e primo lotto AP sono implementati nel diagnostico. Il negativo
di rollback inietta un errore prima della seconda allocazione: uno stack
già pronto viene rilasciato, entrambe le prenotazioni vengono annullate e
il budget viene verificato prima del panic coordinato. Non simula
l'esaurimento reale della RAM fisica.

1. Preemption periodica del task AP e ripristino del contesto.
2. Dispatcher concorrenti e conclusione coordinata del lavoro globale.
3. Stress e regressioni di chiusura C4.

Sono tre obiettivi di implementazione e validazione: alcuni
possono unirsi, i fault possono richiedere un blocco di diagnosi dedicato.
Poi restano C5 (driver su più CPU) e C6 (chiusura K4). K5 userspace e K6
caricamento dei programmi non sono inclusi in questa stima.

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
