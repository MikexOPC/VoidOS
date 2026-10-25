# G7 — Progetto su carta: SMP e user mode

25 ottobre 2026. Documento di design, senza codice. Serve a fissare le scelte prima di rifare i
driver come oggetti (G6), perché lock, ownership e contesti dipendono da qui.

## 1. Stato attuale (verificato nel codice)

- **Un solo core.** Il BSP esegue tutto. La MADT viene letta (la campagna di boot la prova con 1 e
  4 CPU), ma gli AP non vengono avviati.
- **Scheduler cooperativo:** 8 slot di stack da 64 KiB con guardie (`X86_TASK_STACK_*`), nessuna
  preemption. La correttezza di molti moduli si basa su "nessun altro gira adesso".
- **Sincronizzazione:** nessun lock né atomica. Ci sono sezioni con IF=0 (sessione VT-d) e
  contatori `volatile`.
- **Una GDT/TSS** con tre IST (IST1 per i fault e il rilevamento dell'annidamento, IST2 per NMI,
  IST3 per #DF).
- **Spazio di indirizzi, tutto nella metà bassa:**

  | Uso | Dove |
  |---|---|
  | Immagine del kernel | `0x1_0000_0000` (slot PML4 0) |
  | Slot ricorsivo | 128 |
  | Arena di bootstrap, finestra, stack dei task | `0x7000_0000_0000` (slot 224) |
  | Framebuffer, GPU, superficie, IOMMU, DMA, heap | slot 225–230 |
  | Metà alta (slot 256–511) | vuota |

- **Interrupt:** vettori fissi 0x40–0x42 (timer LAPIC, tastiera, mouse) sul BSP, I/O APIC
  instradato verso il BSP.
- **Memoria bassa:** dopo B8 la memoria sotto 1 MiB, pagina 0 esclusa, entra nel pool fisico.
  Gli stack IST stanno a `0x1000`.

## 2. SMP

### S1 — Avvio degli AP
Intel SDM Vol. 3A §8.4.4 (MP Initialization), sequenza INIT–SIPI–SIPI tramite l'ICR del LAPIC.
- **Trampolino:** serve una pagina **sotto 1 MiB** (il vettore SIPI indica una pagina reale).
  Dopo B8 quella memoria può già essere nel pool, quindi la pagina va **riservata prima** che
  l'allocatore la consegni: una regione fissa (per esempio `0x8000`) esclusa durante
  l'inizializzazione del pool.
- Ogni AP passa da modo reale a protetto a long mode, carica lo stesso CR3 iniziale, la propria
  GDT/TSS e il proprio stack, poi segnala "pronto" con un'atomica.
- **Prova:** la campagna con `-smp 4` deve vedere 4 CPU pronte. Oggi "4 CPU configurate" non
  dimostra SMP (direttive-locali, CONSTRAINT 6).

### S2 — Dati per CPU
- **Decisione D2:** per CPU servono `GS.base` e una struttura `KERNEL_CPU`: id, LAPIC id, stack
  del kernel, TSS, IST, task corrente, contatori. Su x86-64 l'alternativa (array indicizzato
  dall'APIC id letto ogni volta) è più lenta, e con user mode `swapgs` serve comunque.

### S3 — Sincronizzazione
- **Decisione D4:**
  - **Spinlock a ticket** con salvataggio di IF (`lock_irqsave`) per i dati toccati dagli IRQ;
  - **atomiche** C11/intrinseche per contatori e flag;
  - nessun `volatile` come sincronizzazione.
- **Ordine dei lock documentato:** una tabella nel repo, verificata nelle build di debug con un
  controllo di livello per CPU.
- **Da rivedere quando arriva SMP:**
  - contatori di generazione del paging e dell'allocatore (G5): diventano atomici, e l'allocatore
    fisico ha un lock;
  - heap: un lock, oppure cache per CPU dello slab in un secondo momento;
  - code VirtIO, sessione VT-d e console: un solo owner (una CPU) oppure un lock per oggetto (G6).

### S4 — Scheduler
- **Decisione D3:**
  - **A (consigliata per la v0):** una coda globale con un lock e la preemption dal timer LAPIC
    per CPU. È semplice da verificare e va bene fino a pochi core.
  - **B:** code per CPU con bilanciamento. Si passa qui quando le misure lo giustificano. La scelta
    resta dietro un'interfaccia `scheduler_pick/enqueue`, come da regola di migrazione.
- Il **context switch** salva lo stato completo: registri, SIMD secondo la politica scelta, GS
  utente e kernel.

### S5 — Cambi di paging su più CPU
- Una modifica del paging condiviso richiede **TLB shootdown**: IPI alle altre CPU, `invlpg` locale,
  attesa delle conferme (Intel SDM Vol. 3A §4.10.5).
- L'API di paging che oggi fa solo `invlpg` locale diventa "invalida su tutte le CPU che usano quel
  CR3". La generazione del paging (G5) avanza dopo le conferme.

### S6 — Interrupt
- Vettori allocati da un registro invece che fissi (0x40–0x42), con l'affinità a una CPU.
- L'I/O APIC e, più avanti, l'MSI dirigono gli IRQ verso una CPU. All'inizio tutti verso il BSP
  basta.

## 3. User mode

### U1 — Spazio di indirizzi
Il problema principale: il kernel occupa la metà bassa.
- **Decisione D1:**
  - **A (consigliata):** spostare il kernel nella **metà alta**, con immagine, arena, slot dei
    device, heap e ricorsivo negli slot 256–511, e lasciare all'utente la metà bassa. È il layout
    standard: separazione netta e SMEP/SMAP semplici. È un refactor grande (loader, linker, tutte
    le costanti di slot, i runner che controllano gli indirizzi), da fare **prima** di qualsiasi
    processo utente.
  - **B:** dare all'utente una porzione bassa libera (per esempio gli slot 1–127). È meno lavoro
    subito, ma mescola kernel e utente nella stessa metà, rende fragili i controlli di range e
    complica KPTI.
- **Ogni processo:** un proprio PML4, con la metà alta del kernel condivisa (stesse voci
  PML4[256–511]).

### U2 — Ingresso nel kernel
- **Decisione D5:** `SYSCALL`/`SYSRET` (MSR STAR, LSTAR, FMASK; Intel SDM Vol. 2B) con `swapgs`
  e uno stack del kernel per thread. `int 0x80` solo come ripiego, se servisse.
- **Ritorno all'utente:** RIP canonico controllato prima di `SYSRET` (errata nota su Intel), RFLAGS
  ripuliti, registri del kernel azzerati.
- **Copie da e verso l'utente:**
  - con controllo del range;
  - con SMAP aperto solo dentro la copia (`stac`/`clac`);
  - con recupero dai page fault tramite una tabella di eccezioni: un #PF dentro una copia
    restituisce un errore, non è terminale (direttive-locali, CONSTRAINT 5).

### U3 — Protezioni
- **Decisione D6:**
  - **SMEP e SMAP** abilitati se CPUID li riporta (leaf 7: EBX bit 7 SMEP, bit 20 SMAP); senza, si
    registra il limite.
  - **NX** è già attivo.
  - **KPTI** viene deciso per modello di CPU e di minaccia. Nella v0 su QEMU non si abilita, ma il
    layout di D1-A lo permette senza un altro refactor.

### U4 — Primo programma
- **Decisione D7:** un programma ELF statico letto dall'ESP, come oggi il kernel, caricato dal
  kernel in un nuovo spazio di indirizzi.
- **Syscall minime:** scrittura sulla console, uscita, attesa. Niente filesystem: arriverà dopo.

### U5 — Fault in modo utente
Un #PF o un #GP con CPL=3 termina il processo, non il kernel. Oggi tutte le eccezioni sono
terminali: serve un ramo "fault utente", distinto dai fault del kernel.

## 4. Vincoli per G6 (driver come oggetti)

Da questo progetto discendono le regole per gli oggetti driver:
1. **Nessuno stato globale nascosto:** tutto lo stato del driver sta in un oggetto con un owner.
2. **Ogni oggetto dichiara il proprio contesto:** solo task, task e IRQ, o una CPU fissa. Il tipo
   di lock dipende da questo: nessuno per un owner unico, `lock_irqsave` se lo tocca un IRQ.
3. **Le risorse (lease, mappature, vettori) sono dell'oggetto** e vengono rilasciate da lui.
4. **Le dipendenze arrivano come parametri:** clock (G4), allocatore, heap (G3). Si chiude così la
   cattura unica del clock rimandata in G4.
5. **Nessuna ipotesi di "unico core"** nei moduli nuovi, anche se SMP non è ancora attivo.

## 5. Ordine proposto

| Passo | Contenuto | Criterio di accettazione |
|---|---|---|
| G6 | Driver come oggetti con le regole del §4, partendo dalla present VirtIO e dalla console | Nessun `static` di stato nei driver migrati; smoke set |
| K1 | Kernel nella metà alta (D1-A) | Smoke set completo con il nuovo layout; nessun indirizzo del kernel sotto lo slot 256 |
| K2 | Lock e atomiche (D4), allocatore e heap con lock | Test host di contesa simulata; smoke set |
| K3 | AP bring-up (S1) e dati per CPU (D2) | `-smp 4`: 4 CPU pronte, 10 boot |
| K4 | Shootdown del TLB (S5), scheduler con preemption (D3-A) | Stress con task su 4 CPU; nessuna corruzione, audit G5 pulito |
| K5 | SYSCALL, SMEP/SMAP, copie con recupero (U2, U3) | Test negativi: puntatori del kernel dalla parte utente rifiutati |
| K6 | Primo processo utente (U4) e fault utente (U5) | Un programma ELF stampa ed esce; un programma che sbaglia viene terminato senza panic |

## 6. Decisioni da approvare

D1-A (kernel nella metà alta), D2 (GS per CPU), D3-A (coda globale con preemption, interfaccia
pronta per B), D4 (ticket spinlock con irqsave e ordine dei lock), D5 (SYSCALL/SYSRET), D6
(SMEP/SMAP da CPUID, KPTI rimandato), D7 (primo programma dall'ESP), e l'ordine
G6 → K1 → K2 → K3 → K4 → K5 → K6.
