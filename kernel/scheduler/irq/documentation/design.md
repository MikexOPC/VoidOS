# Scheduler: IRQ durante l'esecuzione delle task

8 ottobre 2026. L'utente ha scelto IRQ ammessi durante l'esecuzione delle task,
in luogo del profilo con IRQ limitati all'idle del dispatcher. Estensione dello
scheduler BSP cooperativo; gli IRQ notificano eventi, non effettuano preemption.
Questa pagina e un contratto progettuale, non un report di test PASS.

## Baseline e review

[Attesa/risveglio](../../wait/documentation/validation.md): task IF=0, gateway
Rust esclusivo sul dispatcher, stack fisici pinnati e due guardie per task.
Le sole interfacce di wait/watchdog non consentono di abilitare IF nelle task.

La review dell'entry IRQ esistente rileva salvataggio dei quindici GPR diversi
da RSP, frame CPU con RSP e FXSAVE64/FXRSTOR64 prima/dopo C, shadow space32 e
allineamento16. Il problema reale e `frame_owner` in
`arch/x86/kernel/interrupts/return/table.c`: accetta soltanto lo stack principale.
Va esteso a un owner task verificato prima di permettere IRQ su quello stack;
non basta allargare il range numerico agli otto indirizzi riservati.

Il dispatcher attuale e monouso e viene eseguito prima dell'avvio driver.
Il timer ha ownership, subscription, stop/EOI, consume/rearm e watchdog HPET
gia separati. Occorre un punto d'integrazione mentre il timer e effettivamente
armato e il relativo handler ha durata garantita, non dopo il suo cleanup.

## Fonti e differenze rispetto ai riferimenti

[Linux 6.12 wait queues](https://www.kernel.org/doc/html/v6.12/driver-api/basics.html):
la condizione va aggiornata prima della notifica e ricontrollata al risveglio.
La scelta locale e una notifica differita; non importiamo code/lock Linux.
[Rust AtomicU32](https://doc.rust-lang.org/core/sync/atomic/type.AtomicU32.html)
e [memory ordering](https://doc.rust-lang.org/core/sync/atomic/index.html):
pubblicazione Release e consumo Acquire, operazioni RMW esplicite. Queste
operazioni non rendono automaticamente sicuro un payload non atomico su SMP.
[Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html),
edizione093: Vol.2 FXSAVE/FXRSTOR/CLI/STI/IRET; Vol.3A capitolo7, interrupt
in IA-32e; Vol.1 §11.6.6 MXCSR. Prima di nuovo Assembly verificare le sezioni
esatte delle istruzioni, opcodes e prerequisiti contro le copie del modulo.
Registri timer/EOI restano quelli del driver gia documentato, non nuovi offset.

## Contratto scelto

1. Task kernel fidati, BSP, x87/SSE; nessun AP, AVX/AMX/CET, TLS o unwind.
2. IF=1 soltanto durante codice task ammesso. Prima di cedere/attendere/terminare
   il task entra in una sezione IF=0; switch, validazione, mapping, raccolta e
   gateway rimangono IF=0. Al rientro il profilo task ripristina IF=1 nel punto
   autorizzato. Non rimuovere globalmente il controllo IF del gateway.
3. L'handler interrompe lo stack task, salva il contesto completo, riconosce
   owner/stack/generazione correnti e valida source. Nessun salto allo scheduler.
4. Il driver completa acknowledgement/EOI e pubblica una notifica in storage
   atomico distinto dall'Owner Rust. Nessuna allocazione, log o borrow dell'Owner
   nel gestore; ingresso annidato, NMI e pubblicazione da AP fuori contratto.
5. Il dispatcher consuma notifiche con IF=0 dopo aver terminato il RUNNING
   corrente. Una transazione Rust incrementa le epoche e risveglia i WAITING.
   Non simulare un handle produttore quando non esiste una task RUNNING.
6. Le notifiche possono coalescere per evento: il contatore e la condizione del
   driver restano autoritativi. Non promettere una notifica per ogni IRQ o un
   trasferimento di payload. Nessun incremento atomico non controllato in wrap.

Per il primo profilo si usa il timer LAPIC one-shot gia calibrato. HPET resta
la sorgente indipendente per il successivo idle/HLT del dispatcher driver;
durante il compute task la deadline usa clock PM ACPI e un limite di iterazioni.
La task interrotta deve
essere identificata nella prova; un INT software sullo stack principale non
dimostra ricezione hardware durante l'esecuzione di una task.

## Ownership del frame

Validazione task IRQ: fase task attiva, indice/generazione esatti, contesto e
owner stabili, lease fisica unica/pinnata, mapping RW/XD e guardie assenti.
Il frame intero e RSP interrotto devono appartenere al medesimo stack dati;
RIP deve essere codice kernel supervisor RX. Le verifiche devono precedere
la lettura dei campi tramite un puntatore non ancora riconosciuto.
Il dispatcher mantiene il contratto precedente dello stack principale.

L'entry consuma spazio aggiuntivo per frame/GPR/FX e C: il carico deve avere
headroom verificato prima di abilitare IF. Le guardie e gli IST terminali sono
ultima difesa, non una promessa di recupero da stack overflow. Nessuna release
o modifica PTE mentre il task puo essere interrotto o una source resta live.

## Protocollo differito e failure

Pseudocodice non eseguibile:

```text
handler:
    riconosci frame e source
    salva contesto completo prima del C
    ack/EOI e aggiorna la condizione driver
    pubblica bit evento con Release
    ripristina il contesto completo e IRET

dispatcher:
    IF=0, consuma mailbox task, current=NONE
    acquisisci notifiche pending con Acquire
    applica epoche/wakeup in una transazione Rust
    se fallisce: conserva notifiche/owner e termina il profilo
    scegli READY; nessun borrow oltre il ritorno FFI
```

Un errore di source, epoca esaurita o notifica senza binding non e un successo.
Timeout con waiter conserva i loro stack: non usare collect su WAITING.
Chiudere: IF=0 -> mask/stop source -> verifica IRR/ISR/quiescenza -> unbind
subscription -> chiusura pubblicazione -> cleanup risorse possedute. Su errore
conservare owner e handler validi; niente ritorno a storage su stack transitorio.

Un task non cooperante rimane un limite: IRQ attivi non implicano preemption.
Il carico diagnostico deve avere un limite indipendente dalla notifica attesa;
non usare un loop infinito su un bit che il device potrebbe non impostare.
Idle/HLT richiede watchdog armato e protocollo senza finestra di wake perso.

## Gate prima della dichiarazione di completamento

- Host Rust: post/drain, coalescing, bounds, pubblicazione prima/dopo wait,
  batch atomico, overflow e rifiuti immutabili; modello indipendente.
- Host C/Rust: layout, contesto vietato, frame fuori stack, generazione vecchia,
  owner non pinnato/retirato, mapping alterato e ingressi annidati.
- Kernel: IRQ LAPIC effettivo durante una task, IF del frame, RIP/RSP e
  indice/generazione; canary GPR e XMM/FP preservati dopo IRET e successivi yield.
- Dieci boot consecutivi dell'immagine positiva finale, marker, CPU/IRQ/PTE,
  accounting e cleanup completo delle source possedute.
- Negativi: IRQ perso/deadline, errore source, callback tardivo e waiter
  conservati su cleanup fallito; guardie/NX/rollback del profilo precedente.
- Warning come errori, cppcheck, ASan host dove strumentato, file<=400 e
  massimo cinque file diretti; coverage N/D finche non misurata, niente
  dichiarazioni di test fisici o Valgrind non eseguiti.

I risultati del profilo IF=0 non valgono come PASS per questo nuovo contratto.

## Perimetro dell'implementazione v0

Il profilo iniziale ammette il vettore LAPIC 0x41 nella sola sezione Assembly
compute della task worker; C, gateway, attesa, switch e cleanup mantengono IF=0.
Consumer bloccato sull'evento 2, worker che riceve un IRQ, tre dispatch: non e
ancora una politica per IRQ arbitrari in qualunque funzione C o per tutti i driver.
Non ci sono preemption, AP/SMP, user mode o scheduler idle generale.

La source e READY con subscription live mentre vengono preparate le task.
Il worker arma il timer soltanto dopo la preparazione e registra l'intervallo
PM di arm/compute separatamente da allocation, raccolta e attesa HPET: il
vecchio limite del timer non deve misurare questi lavori aggiuntivi.

Il test irq-timeout ferma intenzionalmente la source tramite l'API stop gia
verificata e poi esegue il compute con IF attivo. La deadline/ceiling chiude
IF e termina il profilo: lease dei due stack e binding restano validi, nessuna
raccolta di WAITING o liberazione di una risorsa ancora posseduta.

Le prove eseguite e i limiti di quelle pianificate sono riportati nel
[report di validazione](validation.md).
