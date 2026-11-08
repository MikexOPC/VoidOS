# K5 — Confine tra kernel e userspace

VoidOS x86-64. Per K5-A è stata scelta l'opzione B. Il diagnostico QEMU
sul BSP include il primo payload ring 3 e la prova NMI durante lo
scambio di GS. Le syscall arrivano in un blocco separato.

## Da dove partiamo

K1 ha preparato la metà alta per il kernel. K3 ha introdotto il contesto
per CPU via GS e gli stack IST separati per NMI e double fault. Il percorso
IRQ che riprende il codice interrotto salva GPR e FXSAVE64, entra con
stack del kernel e termina con `IRETQ`. La tabella corrente documenta quel
percorso come ring 0. Il contratto attivo di GS precede lo user mode.
[K1](k1-higher-half.md) · [Contesto per CPU](../../arch/x86/kernel/cpu/percpu/documentation/k3-design.md) ·
[IRQ di ritorno](../../arch/x86/kernel/interrupts/return/table.c) ·
[Ingresso Assembly](../../arch/x86/kernel/interrupts/return/assembly/entry/return.asm).

Linux v6.12 e FreeBSD 14.2 passano a uno stack fidato in software quando
entrano con `SYSCALL`. Entrambi gestiscono separatamente i casi in cui il
frame dice CPL0 mentre GS ha già o ancora la base utente. Questa finestra
conta anche con gli IRQ mascherabili disabilitati. VoidOS può provarla
prima con una coppia di basi nota e una sola CPU esecutrice.
[Linux v6.12](https://github.com/torvalds/linux/blob/v6.12/arch/x86/entry/entry_64.S) ·
[FreeBSD 14.2](https://github.com/freebsd/freebsd-src/blob/releng/14.2/sys/amd64/amd64/exception.S).

## K5-A — Verifica di GS prima di ring 3

### Opzione A, diagnostico dedicato

Un solo probe sul BSP possiede il proprio stato di prova e la pubblicazione
della base GS attesa. Usa memoria supervisor e l'IST NMI già predisposto.
Salva e verifica lo stato iniziale, compresi i due MSR di GS e la IDT.
Il probe installa una vista diagnostica della IDT prima di aprire la
finestra. Verifica che i percorsi di emergenza utilizzabili in quella
finestra possano arrestarsi senza fidarsi di GS o dello stack interrotto.
Il rientro richiede fase armata, RIP atteso e una sola consegna. Il runner
controlla l'iniezione QEMU. Questi vincoli non identificano la sorgente
di un NMI su hardware reale.

Due coppie valide nel diagnostico, indicate come (GS attivo, destinazione
di SWAPGS):

| Fase | Coppia attesa |
| --- | --- |
| Kernel | (base BSP verificata, zero) |
| Finestra simulata | (zero, base BSP verificata) |

Zero rappresenta la base GS utente futura nel solo profilo v0. Il
diagnostico verifica le capacità CPU e che `CR4.FSGSBASE` (bit 16) sia
disattivato. Prima del ring 3 vanno verificati anche i selettori che il
codice utente può caricare in GS e l'assenza di una LDT utente. L'owner
atteso è pubblicato in memoria kernel accessibile senza GS e rimane
immutabile mentre la prova è armata.

Sequenza di progetto, non eseguibile:

1. Preparare il record, verificare lo stack IST e armare la fase con
   interrupt mascherabili disabilitati.
2. Iniettare un NMI in entrambe le fasi in prove distinte. Registrare
   RIP interrotto e valori reali dei MSR prima di usare GS. Nel secondo
   caso, effettuare lo scambio soltanto dopo aver verificato la coppia.
3. Verificare l'accesso al contesto BSP fidato, quindi ripristinare la
   coppia osservata prima di rientrare. La ripresa del codice di prova
   conferma GPR, stack e fase. Nel caso simulato il probe ripristina la
   base kernel prima di tornare a C. Le finestre restano in Assembly.
4. Ripristinare IDT, MSR e stato interrupt. Un timeout o una coppia
   inattesa termina la prova con diagnostica. Se il ripristino è incerto,
   arresto con percorso che non dipende da GS, trattenendo le risorse.

Perimetro proposto: ingresso interrupt x86 e soli adattatori necessari
a contesto BSP, NMI di prova, avvio, build e test centralizzati. Il
diagnostico non entra in ring 3. Rust valida record e invarianti puri,
Assembly gestisce registri e frame, C possiede lo stato della prova.

### Opzione B, primo ring 3 nello stesso blocco

Integrare subito descrittori DPL3, mappature utente, ingresso da ring 3
e prova delle finestre GS. Mostra prima un passaggio di privilegio reale,
ma lega l'esito alla GDT, al paging utente e alla gestione dei fault.
L'opzione A rende quei guasti distinguibili e lascia uno scenario di
regressione riutilizzabile per le fasi successive.

Nel diagnostico B la prova parte dopo il ritiro della mappatura bassa del
loader. Una root privata usa quattro pagine di tabelle, una pagina utente
RX e una pagina di stack utente RW/NX con guardia. La metà alta resta
supervisor. La finestra usata per preparare le pagine viene chiusa prima
di attivare la root. Il payload usa IF=0 e IOPL=0. I fault entrano su
IST1, gli NMI su IST2. Gli ingressi inattesi arrestano la prova senza
accedere al contesto per CPU.

Il diagnostico accerta quattro fault da CPL3. `CLI` genera #GP, una
lettura di una pagina supervisor genera #PF, l'esecuzione sullo stack
NX genera #PF e `UD2` attesta il ritorno al payload dopo l'NMI. I fault
riportano al chiamante diagnostico. Al termine vengono ripristinati
root, IDT, GDT e GS, quindi le sei pagine vengono azzerate e restituite.
Questa variante non prova ancora la consegna degli IRQ ordinari da
CPL3 su uno stack scelto tramite RSP0.

### Gate prima del blocco seguente

- Host: coppie valide, basi inattese, frame non attestato, doppia
  ammissione, timeout e ripristino. Nessuna modifica prima del preflight.
- QEMU/OVMF: dieci boot consecutivi del diagnostico positivo. La
  prova deve osservare un NMI reale nei due punti previsti, incluso CPL0
  con GS simulato. Un NMI consegnato altrove non vale come prova.
- Negativi separati: stato inatteso respinto, nessun accesso per CPU
  da base incerta, nessun ritorno con stack o GS incoerente. Gate
  ordinario e regressione panic SMP sul profilo esistente.

## K5-B — IRQ ordinario mentre gira ring 3

Stato: implementato nel profilo diagnostico QEMU/OVMF con una vCPU.
[Validazione](k5-validation.md).

Obiettivo: provare che un IRQ del timer LAPIC interrompe davvero il
payload a CPL3, entra sullo stack indicato da `TSS.RSP0`, salva il
contesto in Assembly, ripristina il GS kernel, riceve l'EOI e riprende
il payload con GPR e stato SIMD intatti.

### Cosa dice l'hardware

- Un interrupt gate con cambio di privilegio in IA-32e carica RSP da
  `TSS.RSP0` quando l'IST del gate è zero, imposta SS a NULL e spinge
  SS, RSP, RFLAGS, CS, RIP. Il gate azzera IF. Intel SDM 3A §7.14.4
  (stack switching in IA-32e) e §7.14.2 (frame a 64 bit).
- RSP0 sta all'offset 4 della TSS a 64 bit. SDM 3A §8.7 (TSS a 64 bit).
- `SWAPGS` scambia `IA32_GS_BASE` (C0000101h) e `IA32_KERNEL_GS_BASE`
  (C0000102h); è lecito solo a CPL0. SDM 2B, voce SWAPGS.
- Timer LAPIC: LVT Timer a 320h, Initial Count 380h, Divide 3E0h; EOI
  scrivendo zero a B0h. SDM 3A §12.5.4 e §12.8.5. Il driver
  [timer LAPIC](../../arch/x86/kernel/timer/lapic/timer.h) possiede già
  questi accessi: K5-B li usa tramite la sua interfaccia, senza
  duplicarli.

### Decisioni proposte

1. **Sorgente IRQ: timer LAPIC del BSP in modalità one-shot**, vettore
   del registro IRQ esistente. Il PIT è escluso: richiede l'I/O APIC e
   non aggiunge copertura al percorso CPL3. Alternativa scartata per v0:
   periodico, perché più consegne rendono ambiguo il conteggio.
2. **Gate IRQ del diagnostico con IST zero e DPL0.** Solo così la CPU usa
   RSP0; con IST diverso da zero la prova non distinguerebbe nulla. Fault
   e NMI restano su IST1/IST2 come in K5-A.
3. **Stack RSP0 dedicato al probe**: una pagina supervisor RW/NX con
   guardia, posseduta dal probe per la durata della prova. Il probe salva
   il valore RSP0 precedente e lo ripristina prima di restituire le pagine.
   Lo stack RSP0 per CPU permanente arriva con K6, quando esistono i
   processi.
4. **Ordine in ingresso (Assembly)**: leggere CS dal frame; se RPL è 3,
   `SWAPGS`; salvare GPR e `FXSAVE64` sullo stack RSP0 come nel
   [percorso IRQ esistente](../../arch/x86/kernel/interrupts/return/assembly/entry/return.asm);
   poi chiamare C. In uscita, ordine inverso: `FXRSTOR64`, GPR, `SWAPGS`
   solo se il frame torna a RPL 3, `IRETQ`. L'EOI lo scrive il driver del
   timer prima del ritorno, non l'Assembly.
5. **Finestra del payload**: il payload esegue `IF=1` solo in un tratto
   delimitato da etichette, con un ciclo che modifica GPR e XMM con
   valori noti. Fuori da quel tratto resta `IF=0`. IOPL resta 0.
6. **Rust** valida il frame osservato: CS = selettore codice utente con
   RPL 3, SS utente, RIP dentro il tratto IF=1, RSP dentro lo stack
   utente, RFLAGS.IF = 1, RSP di ingresso uguale a RSP0 meno il frame.
   C possiede lo stato della prova, Assembly i registri.

Sequenza di progetto, non eseguibile:

1. Preflight: salva RSP0, IDT, GS e LVT timer; verifica che il timer sia
   libero e mascherato; prepara la pagina RSP0.
2. Installa la IDT diagnostica, scrive RSP0, arma il timer one-shot.
3. `IRETQ` a CPL3; il payload apre il tratto IF=1 e gira finché un
   contatore kernel vede la consegna, con deadline dal clock calibrato.
4. Nel handler: valida frame e GS, registra la consegna, EOI, ritorno.
5. Il payload verifica i propri GPR e XMM, poi esegue `UD2` per tornare
   al kernel come in K5-A.
6. Ripristino: timer mascherato, LVT, RSP0, IDT, CR3, GS; azzera e
   restituisce le pagine. Ripristino incerto → arresto senza GS.

### Differenze emerse nell'implementazione

- **EOI.** I validatori del driver richiedono la mappatura ricorsiva, assente
  nella root privata. Il timer e il controllo della mappatura LAPIC restano
  al driver fuori dalla root privata. Durante l'IRQ il probe usa le primitive
  native `x86_lapic_native_read` e `x86_lapic_native_eoi` e verifica l'ISR.
- **Avvio.** K5-B richiede LAPIC e timer calibrato: parte con un hook di test
  in `x86_timer_run`, dopo il cleanup, solo nel profilo K5. Non è più vero
  che il driver resti intatto: cambiano sei righe sotto `#ifdef`.
- **IF.** Con IOPL=0 il payload non può usare `STI`. IF=1 viene dal frame
  `IRETQ` per tutto il modo 5. L'IRQ può quindi arrivare da subito: la finestra
  accettata va dall'inizio del payload alla fine del ciclo.
- **RF.** L'immagine salvata di RFLAGS può avere RF=1. QEMU lo riporta
  sull'IRQ e il validatore lo ammette, come per i fault.
- **Stack.** La pagina RSP0 è la settima pagina, a 0x404000, nella metà bassa
  della root privata e senza bit utente. La guardia è a 0x403000.

### Casi da provare

- Positivo: almeno una consegna con RIP utente nel tratto IF=1,
  ingresso su RSP0, GS kernel nel handler, registri intatti dopo il
  ritorno, una sola consegna per arm.
- NMI durante il handler IRQ entrato da CPL3 (GS già kernel) e NMI nel
  payload con IF=1: entrambi devono riprendere.
- Negativi separati: IRQ consegnato a CPL0 durante il probe (frame RPL 0)
  respinto come prova; RSP di ingresso diverso da RSP0 → fallimento;
  timeout senza consegna → arresto diagnostico, non PASS; registro
  alterato nel payload → fallimento rilevato.
- Host: validazione Rust del frame con casi validi, CS RPL 0, RIP fuori
  tratto, IF=0, RSP fuori stack, overflow di indirizzi.
- QEMU/OVMF: dieci boot consecutivi del positivo (interrupt e
  transizioni di privilegio), più regressioni ordinaria e panic SMP 3/3.
  Una vCPU, come K5-A.

### Perimetro

Probe in `test/arch/x86/kernel/interrupts/privilege/`, ingresso IRQ del
diagnostico nell'Assembly del probe, uso del driver timer tramite la sua
interfaccia. Nessuna modifica al percorso IRQ ordinario del kernel.
Limite dichiarato: un solo IRQ, una sola CPU, nessuna preemption o
cambio di task; la consegna da CPL3 su AP resta per K6.

## Blocchi successivi

1. **K5-B**: vedi sezione sopra.
2. **K5-C, prima syscall.** Una chiamata senza puntatori. Definire ABI,
   selettori e stack prima di abilitare `IA32_EFER.SCE`. Proposta di
   rollout per D5: ingresso `SYSCALL`, ritorno iniziale via `IRETQ`
   con frame verificato. Valutare `SYSRETQ` come percorso separato dopo
   prove su RIP non canonico e fault durante il ritorno.
3. **K5-D, accessi utente.** Convalidare range, permessi effettivi,
   durata delle pagine, bounds e recupero da #PF soltanto nelle
   istruzioni autorizzate. Abilitare SMEP/SMAP dopo verifica CPUID e
   prove negative, con AC ripulito ai confini. Decidere KPTI sul modello
   di minaccia prima di promuovere il profilo oltre il diagnostico.

K6 collega queste primitive al primo processo ELF. La sicurezza
architetturale del diagnostico e le mitigazioni speculative per codice
non fidato hanno criteri distinti. [Roadmap G7](g7-smp-user.md) ·
[Linux v6.12, PTI](https://docs.kernel.org/6.12/arch/x86/pti.html).

## Riferimenti hardware

- Intel SDM 253668-093US, vol. 3A, §§7.14.2, 7.14.4–5 e §10.7. Frame
  IA-32e, selezione stack, IST a tre bit, TSS e RSP0. §§2.5 e 5.6
  per FSGSBASE, SMEP e SMAP. [Vol. 3A](https://cdrdv2.intel.com/v1/dl/getContent/671190).
- Intel SDM vol. 4, tabella 2-2. `IA32_GS_BASE` = `0xC0000101`,
  `IA32_KERNEL_GS_BASE` = `0xC0000102`. Il primo dà la base attiva,
  il secondo la destinazione dello scambio. [Vol. 4](https://cdrdv2.intel.com/v1/dl/getContent/671098).
- Intel SDM, voci `SWAPGS`, `IRET/IRETD/IRETQ`, `SYSCALL` e `SYSRET`.
  `SYSCALL` salva il RIP seguente in RCX e i flag in R11, senza salvare
  o cambiare RSP. [Indice Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
