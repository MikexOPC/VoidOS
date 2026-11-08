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
- RSP0 sta all'offset 4 della TSS a 64 bit. SDM 3A §10.7, figura 10-11.
- `SWAPGS` scambia `IA32_GS_BASE` (C0000101h) e `IA32_KERNEL_GS_BASE`
  (C0000102h); è lecito solo a CPL0. SDM 2B, voce SWAPGS.
- Timer LAPIC: LVT Timer a 320h, Initial Count 380h, Divide 3E0h; EOI
  scrivendo zero a B0h. SDM 3A §13.5.4 e §13.8.5. Il driver
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
- **Consegne.** Il primo ingresso riceve tre IRQ, riarmati dall'handler; il
  secondo uno, perché un NMI iniettato nella finestra CPL3 non cada nell'handler.
  Ogni frame è validato prima dell'EOI.
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

## K5-B2 — IRQ da ring 3 su un AP

Stato: implementato nel profilo `k5-ap`, QEMU/OVMF, tre vCPU.
[Validazione](k5-validation.md).

Obiettivo: ripetere la prova K5-B su AP1 e AP2, uno alla volta. La prova
deve mostrare che TSS, RSP0, GS, LAPIC e stack usati sono quelli della CPU
che esegue, e che il percorso non dipende dal BSP. Serve almeno `-smp 3`.

### Da dove partiamo

- Ogni AP ha GDT e TSS propri nella pagina metadati del suo slot, con RSP0
  e tre IST. La sua GDT non ha descrittori utente.
- Il servizio AP riceve comandi dal BSP tramite mailbox ed è in ciclo idle.
- Il LAPIC è allo stesso indirizzo virtuale su ogni CPU, ma ogni CPU vede
  il proprio. La calibrazione del BSP è già riusata da `ap-timer`.

### Problemi nuovi rispetto al BSP

1. **NMI.** Il comando `nmi` del monitor QEMU va verificato: se raggiunge
   tutte le vCPU, colpisce anche BSP e AP fuori prova. Per questo il runner
   non può più iniettarlo.
2. **Log.** Nell'handler dell'AP non si scrive sulla seriale. Il runner non
   può quindi coordinare le finestre: lo fa il BSP.
3. **Shootdown e panic.** Sulla root privata l'AP usa la IDT del probe. Uno
   shootdown o una richiesta di panic in arrivo lo fermerebbero.
4. **Stato condiviso.** Le variabili `k5_*` sono globali e hanno un solo
   owner.

### Decisioni proposte

1. **Avvio con un comando del servizio.** Nuova operazione mailbox
   `XIS_K5_IRQ`, presente solo nel profilo di test. Alternativa scartata:
   un hook nell'avvio dell'AP, che girerebbe prima che il servizio sia pronto
   e non proverebbe il kernel nello stato di esercizio.
2. **Una CPU alla volta.** Il BSP pubblica owner, root e pagine, comanda un
   AP, attende l'ack con deadline, verifica e passa al successivo. Così le
   variabili `k5_*` restano a owner unico. La concorrenza fra CPU in ring 3
   appartiene a K6.
3. **Risorse per prova.** Il BSP prepara root e sette pagine, che riusa o
   rilascia dopo ogni AP. L'AP carica una GDT privata con i descrittori
   utente e il proprio TSS, poi una IDT del probe.
4. **RSP0.** Come su K5-B: pagina a 0x404000 nella root privata. Il campo è
   quello della TSS dell'AP, letta dal descrittore e confrontata con
   `X86_AP_METADATA(cpu) + X86_AP_TSS_OFFSET`. Il valore precedente torna al
   suo posto a fine prova.
5. **Timer.** Stesso reload del BSP, LAPIC locale, vettore `XIS_TIMER_VECTOR`
   (D1h) già riservato agli AP. Il controllo dell'ISR dipende quindi dal
   vettore: banco 160h, bit 17.
6. **NMI mirato dal BSP.** Il BSP invia un IPI con delivery mode NMI
   all'APIC ID dell'AP, senza runner. Prima attende che l'handler pubblichi
   l'ingresso nella finestra. Per CPL3 attende l'ultimo IRQ completato; il
   payload aspetta l'NMI nel suo ciclo. Più vicino all'hardware reale del
   comando del monitor.
7. **Quiescenza.** Il BSP non emette shootdown mentre una prova è armata, e
   lo verifica sul contatore `Shootdowns`. Un panic durante la prova ferma
   l'AP sul percorso `x86_k5_stop`: il report lo mostra come ack mancante e
   le risorse restano trattenute. È accettabile solo in questo profilo.
8. **Profilo separato.** Nuova campagna `k5-ap`, tre vCPU. `k5-privilege`
   resta invariata e a una vCPU.

### Sequenza, non eseguibile

1. BSP: fine dei gate K5-A e K5-B, reload disponibile, AP idle.
2. BSP: prepara root e pagine, pubblica owner = contesto dell'AP e numero
   della CPU, comanda `XIS_K5_IRQ`.
3. AP: verifica indice, GS e TSS, salva i propri stati, entra in ring 3.
4. AP: riceve tre IRQ; nel primo pubblica l'attesa e riceve l'NMI dal BSP.
   Seconda entrata: un IRQ e un NMI nel payload a IF=1.
5. AP: ripristina timer, RSP0, GDT, IDT e root; torna al ciclo idle e
   conferma con l'ack.
6. BSP: legge i risultati, azzera e rilascia le pagine, controlla il budget.
   Prova un `PING` all'AP per mostrare che il servizio è ancora vivo.

### Casi da provare

- Positivo, dieci boot: quattro IRQ a `cpl=3` per AP, otto in tutto. Per ogni
  AP: GS uguale al contesto di quella CPU, indice corretto, TSS di quello
  slot, RSP di ingresso atteso, due NMI, stato FPU/SIMD intatto, `PING` dopo
  la prova, poi `KERNEL_READY` o marker finale.
- Prova di non dipendenza dal BSP: la traccia BSP non deve mostrare D1h
  durante le prove, e il timer locale del BSP resta mascherato.
- Negativi, tre boot ciascuno: GS di un'altra CPU; TSS o RSP0 di un'altra
  CPU; nessun NMI dal BSP; AP che non conferma, con arresto alla deadline;
  shootdown durante la prova, respinto senza invio.
- Regressioni: `k5-privilege`, `ap-timer`, `ap-runtime`, `ap-runtime-panic`.

### Perimetro e limiti

Tocca il servizio percpu con un'operazione sotto `#ifdef`, il probe K5, build
e campagne. Il percorso IRQ ordinario degli AP non cambia. Non prova ring 3
simultaneo su più CPU, preemption o processi: restano a K6. La prova resta su
QEMU TCG.

### Differenze emerse nell'implementazione

- **Comando.** La mailbox ha solo due bit per l'operazione, tutti usati. Il
  profilo riusa il codice 2 come `XIS_K5_IRQ`, libero fuori da `ap-timer`.
- **Punto di ingresso.** La prova parte dal gestore dell'IPI, dopo il suo EOI e
  prima dell'ack: senza EOI l'ISR di E1h bloccherebbe D1h.
- **Quiescenza.** Il contatore `Shootdowns` è confrontato solo sulla singola
  prova: il rilascio delle pagine ne emette di legittimi fra un AP e l'altro.
- **Shootdown durante la prova.** Il negativo lo invia davvero e il BSP lo rileva
  per ack mancante, invece di bloccarlo prima dell'invio.
- **Panic.** L'AP in prova non distingue l'NMI di panic da quello di test: nella
  finestra lo tratta come test e continua. Il panic resta riportato dal BSP.
- **Portata di `nmi`.** Il comando del monitor non è più usato, quindi non è stata
  verificata la sua portata. L'invio usa la stessa forma ICR del panic IPI.

## K5-C — Prima syscall

Stato: implementato nel profilo `k5-syscall`, QEMU/OVMF, una vCPU.
[Validazione](k5-validation.md).

Obiettivo: il payload a ring 3 chiama il kernel con `SYSCALL`, senza
puntatori, e riprende con il risultato. Il kernel respinge i numeri sconosciuti
e non riprende mai da un frame non valido. Fuori dalla prova `SYSCALL` resta
disabilitata.

### Cosa dice l'hardware

- Intel SDM 3A §6.8.8: CS viene da `IA32_STAR[47:32]`, SS è quel valore più 8,
  RIP da `IA32_LSTAR`. I flag sono mascherati con `IA32_FMASK`.
- `SYSCALL` non cambia RSP e non scrive in memoria. RCX riceve il RIP di ritorno
  e R11 i flag utente. Fino al cambio esplicito, RSP è controllato dall'utente.
- Con la GDT del probe `IA32_STAR[47:32]` = 8 dà CS 08h e SS 10h, come richiesto.
- Intel SDM 2B, SYSCALL: conferma RCX, R11 e RSP non salvato; `IA32_FMASK` è
  C0000084h. Gli indirizzi di STAR, LSTAR e CSTAR non sono ricontrollati su Vol. 4.

### Decisioni proposte

1. **ABI v0.** Numero in RAX, un argomento in RDI, risultato in RAX. Cambiano
   solo RAX, RCX e R11. Due chiamate: `ECHO` restituisce `RDI + 1`, `CPU`
   restituisce l'indice della CPU. Un numero sconosciuto restituisce -1 e non è
   un fault. Il valore è provvisorio: la tabella degli errori arriva con K6.
2. **Ritorno con `IRETQ`.** Più lento di `SYSRETQ`, ma senza il suo caso noto:
   con RCX non canonico il fault nasce a CPL0 con RSP utente. Il percorso è già
   provato da K5-B. `SYSRETQ` resta un'ottimizzazione da misurare più avanti.
3. **Ingresso, Assembly.** Primo passo `SWAPGS`, poi salvataggio di RSP utente e
   caricamento dello stack kernel, quindi frame come quello degli IRQ, GPR e
   `FXSAVE64`. Solo dopo si chiama codice C o Rust.
4. **Dove salvare RSP utente.** In v0 una variabile del probe, valida perché la
   prova gira su una CPU alla volta. Alternativa rimandata: un campo per CPU in
   `X86_CPU_LOCAL`, che cambia un'ABI validata e serve solo con ring 3 simultaneo.
   K6 dovrà introdurlo.
5. **Stack.** La stessa pagina RSP0 del probe. Con IF=0 per tutta la syscall non
   c'è annidamento di IRQ. NMI e fault restano sui propri IST.
6. **`IA32_FMASK`.** Azzera TF, DF, IF, IOPL, NT e AC. Così l'ingresso non subisce
   IRQ né #DB a passo singolo.
7. **Validazione, Rust.** Numero di chiamata e frame di ritorno: CS 33h, SS 2Bh,
   RIP canonico e nella pagina utente, RSP nello stack utente, flag con IF=1 e
   IOPL, NT, VM a zero. Un frame non valido ferma la prova. Non si tenta `IRETQ`.
8. **Abilitazione limitata.** Il probe salva `EFER` e i quattro MSR, imposta SCE
   e `CSTAR` verso l'arresto, poi ripristina tutto. Nel probe non esistono
   segmenti utente a 32 bit.
9. **Finestra NMI.** Solo nel profilo di test, un'attesa all'inizio dell'ingresso
   prima di `SWAPGS`, con contatore in memoria per non toccare registri e stack.
   Prova il caso peggiore: CPL0, GS utente, RSP utente.
10. **Una consegna IRQ dopo.** Un IRQ del timer subito dopo il ritorno mostra che
    syscall e percorso IRQ convivono.

### Sequenza, non eseguibile

1. Il probe, a IF=0, salva gli MSR e abilita SCE.
2. Il payload carica valori noti, chiama `ECHO`, verifica risultato e registri
   conservati, anche FPU/SIMD.
3. Chiama `CPU`, poi un numero sconosciuto, e verifica -1.
4. Altra entrata con NMI nella finestra di ingresso e nel payload.
5. `UD2` riporta al kernel. Ripristino di MSR, EFER e pagine.
6. Il payload senza SCE riceve #UD: prova che l'abilitazione è davvero limitata.

### Casi da provare

- Positivo, 10 boot sul BSP: tre chiamate, NMI, IRQ dopo il ritorno, poi
  `KERNEL_READY` con `SCE=0`.
- Negativi, 3 boot ciascuno:
  - frame alterato con RIP non canonico, RIP kernel, CS errato, IOPL 3: arresto
    senza `IRETQ`;
  - stack utente non valido in RSP: l'ingresso non lo usa e la prova passa;
  - TF attivo prima di `SYSCALL`: nessun #DB nel kernel prima del ritorno;
  - `SWAPGS` mancante, nessun NMI, registro non conservato: rilevati.
- Host: test Rust su numeri e frame, con confini e overflow.
- Regressioni: `k5-privilege`, `k5-ap`, `ap-runtime`, `ap-runtime-panic`.

### Differenze emerse nell'implementazione

- **Profilo separato.** `k5-syscall` non esegue K5-A e K5-B, così `k5-privilege`
  resta invariata. Parte con un hook in `x86_timer_run`, come K5-B.
- **File.** La syscall sta in `syscall.c`, `assembly/syscall/entry.asm` e
  `rust/syscall/validation.rs`, con un proprio payload. Non è servito dividere
  `assembly/probe/entry.asm`, che resta a 490 righe.
- **RSP non valido.** Il design diceva che la prova sarebbe passata. Il validatore
  controlla però RSP nel frame di ritorno: la prova si ferma senza fault, e questo
  mostra che l'ingresso non ha usato lo stack utente.
- **TF.** Con TF attivo nessun #DB a CPL0; il frame lo conserva, quindi #DB arriva
  a CPL3 dopo il ritorno e il probe si ferma.
- **Flag.** AC attivo nei flag utente è respinto: il payload non lo usa e la scelta
  va rivista con K5-D.

### Perimetro e limiti

Solo il probe K5, build e campagne, con il kernel ordinario a `SCE=0`. Dimensione:
`probe.c` è già a 596 righe, quindi la parte syscall va in file separati, e
`entry.asm` si divide per funzione. Solo BSP e una vCPU in v0: gli AP richiedono
MSR per CPU e si aggiungono dopo. Niente puntatori utente, cioè K5-D, né SMEP,
SMAP o KPTI. IF=0 in tutta la syscall, quindi niente attese né preemption.

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
