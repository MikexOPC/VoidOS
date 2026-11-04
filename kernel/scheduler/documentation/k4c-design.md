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

Per il prossimo collegamento, la proposta è conservare una
continuazione nello stack del task. Il ramo Assembly dopo il
ritorno dell'handler può sospenderla usando lo switch esistente.
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

## Fonti

- [Intel SDM Vol. 3A, rev. 078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
  §6.8.1 STI/HLT, §6.14 entry e IRETQ a 64 bit, §11.5.4 timer LAPIC
  (modalità periodica, divisore, LVT), §11.8.5 EOI.
- [Linux v6.12, kernel/sched/core.c](https://github.com/torvalds/linux/blob/v6.12/kernel/sched/core.c):
  riferimento per `need_resched` e preemption all'uscita dell'IRQ.
  Nessun codice copiato.
- [xv6-riscv, kernel/proc.c](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/proc.c):
  scheduler per CPU con lock globale e switch verso il dispatcher.
