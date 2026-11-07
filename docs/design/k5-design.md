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

## Blocchi successivi

1. **K5-B, IRQ da ring 3.** Estendere il payload per ricevere un IRQ
   mascherabile sullo stack kernel attestato da RSP0. Verificare il
   riconoscimento di CPL, GS per CPU, frame `IRETQ`, contesto SIMD,
   acknowledgement e ripresa, con NMI in entrambe le finestre.
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
