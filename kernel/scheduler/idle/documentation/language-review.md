# Review mirata: Rust, C e Assembly nello scheduler

8 ottobre 2026. Perimetro: core scheduler Rust, gateway C ABI, contesto x86,
ponte IRQ e adapter idle. Non e un audit dell'intero kernel o dell'entry IRQ
esterna al modulo. Nessun sorgente eseguibile modificato in questa review.

## Valutazione

La separazione attuale e utile: Rust possiede la macchina a stati e le code;
C possiede integrazione, lease native e mapping; Assembly realizza switch e
cattura del contesto. Nessun difetto bloccante dimostrato nel codice esaminato.
Questo esito non equivale a una prova di assenza di vulnerabilita.

Owner e queue usano array bounded, handle con generazione e transazioni su
candidato: il fallimento non pubblica uno stato parziale. Il gateway limita
UnsafeCell a un accesso esclusivo, con callback di validazione prima del borrow.
Le notifiche ISR usano storage atomico distinto; nessun borrow Owner attraversa
uno switch o l'attesa hardware. Questi sono i confini da conservare.

UnsafeCell permette mutabilita interna ma non elimina i vincoli di aliasing o
le race: [contratto ufficiale Rust](https://doc.rust-lang.org/core/cell/struct.UnsafeCell.html).
L'FFI richiede ABI, durata e precondizioni verificate dal chiamante:
[Rustonomicon FFI](https://doc.rust-lang.org/nomicon/ffi.html).
Il guard atomico non dimostra che l'intero scheduler supporti SMP, IRQ o NMI.

Il backend switch conserva registri nonvolatili e immagine x87/XMM tramite
FXSAVE64/FXRSTOR64. Header C e controlli statici fissano dimensioni e offset;
il raw backend resta privato e richiede contesti/stack gia validati.
La convenzione di chiamata e quella
[Microsoft x64](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170).
Il contratto corrente esclude AVX/AMX, CET, migrazione TLS e preemption IRQ:
non estendere le istruzioni salvate senza progettare e verificare quel profilo.

## Miglioramenti prima delle task persistenti

| Priorita | Evidenza nel codice | Miglioramento proposto | Criterio di accettazione |
| --- | --- | --- | --- |
| P1 | integration/kernel.c:309 lega fine sessione a due task, contatori e irq_profile; idle/idle.c usa evento 3 e goal 2 | Separare orchestration diagnostica da ciclo dispatcher riutilizzabile | Ripetere wait/wake/exit senza dipendere da indici o conteggi diagnostici; preservare tutti i vecchi profili |
| P1 | pending.rs:3 dichiara notification only; post usa fetch_or | Definire per ogni futura source una condizione persistente o una coda bounded distinta | Burst coalescenti non perdono dati; overflow esplicito e ownership del payload documentata |
| P1 | owner.rs:164 collect accetta Finished; manca un comando di arresto per Waiting | Progettare arresto cooperativo in Rust, prima di aggiungere teardown native | Wake/arresto concorrenti serializzati dal dispatcher; nessuna raccolta di stack RUNNING o ancora referenziati |
| P2 | context.asm contiene offset numerici duplicati rispetto a context.h | Valutare costanti Assembly generate da una descrizione ABI unica e controllo in build | Ogni campo usato dallo switch resta verificato; mismatch provoca errore di build |
| P2 | gateway.rs e adapter C usano anche codici numerici privati | Nomi per operazioni/errori/profili e contratto condiviso verificabile | ABI invariata e casi invalidi ancora rifiutati; evitare un secondo stato parallelo |
| P3 | owner.rs:62 usa contains dentro scansione dei slot; transaction copia Owner | Misurare prima di ottimizzare validazione e rollback | Benchmark definito; nessuna rimozione di invarianti per una velocita presunta |

La validazione comprende scansioni quadratiche rispetto alla capacita, oggi
limitata a otto task; Owner occupa 464 byte. Non e stato misurato un collo di
bottiglia. Aumentare capacita o frequenza richiede misure; mantenere il rollback
e preferibile a una riscrittura anticipata.

## Allocazione dei linguaggi proposta

- Rust: attese ripetute, generazioni, arresto cooperativo, code di notifiche o
  payload bounded quando il contratto del driver sara definito. Core senza heap
  e API a valori/handle, con unsafe confinato al gateway.
- C: lifetime delle lease, verifica dei mapping e collegamento dei driver;
  rifiuti/rollback espliciti. Un wrapper Rust su un raw pointer non certifica
  da solo una pagina presente, il pin o il lifetime di una risorsa hardware.
- Assembly: ingresso/uscita CPU, switch, istruzioni privilegiate e test di
  preservazione. Non aggiungere scheduling policy o code in Assembly.

Non e utile inseguire una percentuale di linguaggi. Non viene proposta una
migrazione globale dell'allocatore o una nuova architettura hardware in questo
blocco. Le scelte di arresto e del dispatcher persistente richiedono il design
discusso previsto da direttive-locali, poi implementazione nello stesso modulo.

## Prove di questa review

Rieseguito kernel/scheduler/scripts/test.ps1 con toolchain supportata:
22/22 test Rust debug, 22/22 release, Clippy con warning gate, 520699 controlli
FFI C/Rust con C ASan, 80000 coppie select/yield e cppcheck senza diagnostiche
error/warning/performance/portability. Evidenze:
`kernel/build/scheduler-host-7bab943c61d54fc28dd6f2b8ba120f3e/`.

I callback CPU dei test FFI sono mock; Rust/Assembly non sono strumentati da
ASan. Coverage linee/branch N/D; Valgrind non eseguito. Non rieseguiti QEMU o
test backend Assembly in questa review senza modifiche eseguibili. Le prove
firmware del blocco precedente sono nel [report idle](validation.md), con
manifest dei sorgenti e hash dell'immagine: non sono nuovi boot della review.

Conclusione: mantenere il confine tra linguaggi; prossimo intervento da
progettare e la separazione dispatcher/profili insieme al contratto di attesa
ripetuta e arresto cooperativo, prima di collegare driver persistenti.
