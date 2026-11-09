# K6 — Primo processo utente

Stato: design approvato con le correzioni di revisione. K6-A, K6-B e K6-C implementati, K6-D non iniziato.
Il contratto delle due root è provato in K6-C, nel profilo `k6-process`.

Obiettivo: il kernel ordinario carica un programma ELF nel suo spazio di indirizzi,
lo esegue a ring 3 e ne raccoglie le risorse. Il test decisivo usa più programmi:
uno stampa ed esce, uno provoca un fault, uno gira all'infinito. Il kernel li termina
e continua a funzionare. [Roadmap G7](g7-smp-user.md) · [K5](k5-design.md).

Direttiva: valorizzare al massimo Assembly e Rust. Build con Meson e Ninja. Eliminare
PowerShell il più possibile, anche nei test, usando C, C++ e Rust.

## Da dove partiamo

K5 ha provato i meccanismi in profili di test separati. Il kernel ordinario resta a
`SCE=0`, senza SMEP e SMAP, senza descrittori utente e con IRQ pensati per ring 0.

Per la build: 136 script PowerShell (10.700 righe) e 92 script Python (10.900 righe).
Meson e Ninja sono stati provati solo sui test host dell'editor di riga. Il planner Rust
copre tre profili del kernel, senza eseguire tutto. Meson richiede Python.

## Linguaggi in K6

| Parte | Linguaggio | Contenuto |
| --- | --- | --- |
| ELF, syscall, processi, accounting | Rust | Parser, tabella chiamate, stati del processo, piano delle mappature |
| Ingressi e ritorni | Assembly | `SYSCALL`, IRQ e fault da CPL3, copia, cambio di CR3 |
| Programmi di prova | Rust e Assembly | ELF statici, `_start` in Assembly |
| Collegamento al kernel esistente | C, solo sottile | Finestra di pagina, allocatore, console, scheduler |
| Test host e runner | Rust | Sostituiscono PowerShell e Python per K6 |

Non riscrivo in Rust i moduli C esistenti: K6 li chiama attraverso interfacce piccole.

## Decisioni proposte

1. **Programma incorporato nell'immagine, non letto dall'ESP.** Dopo `ExitBootServices`
   il kernel non legge più file, e D7 richiederebbe di cambiare il loader per consegnare
   moduli. Il blob resta un vero ELF, quindi parser e caricamento sono gli stessi. La
   lettura da ESP arriva con il primo modulo o filesystem. Questa è una deviazione da D7.
2. **ELF rigoroso.** Solo ELF64, x86-64, `ET_EXEC`. Solo `PT_LOAD`. `PT_INTERP` e
   `PT_DYNAMIC` respinti. Nessun segmento W+X. Allineamento a pagina, `filesz <= memsz`,
   niente sovrapposizioni o overflow, tutto sotto il limite utente, entry in un segmento
   eseguibile, massimo di segmenti e pagine. Rust valida tutto prima di allocare.
3. **Il kernel gira sulla propria root, non su quella del processo.** I validatori del
   kernel e lo shootdown richiedono la mappatura ricorsiva e la root del kernel: K5-B ha
   mostrato il problema e `xis_native_index` lo impone.
   - Scelta: ogni ingresso da CPL3 passa alla root del kernel, ogni ritorno a quella del
     processo. La copia utente riattiva quella del processo solo per l'istruzione di
     copia, con IF=0.
   - Alternativa scartata: mappatura ricorsiva in ogni root e validatori rilassati, che
     toccherebbe molti moduli già chiusi.
   - Costo: un flush del TLB a ogni ingresso, senza PCID. Da misurare. Non è KPTI, perché
     la root del processo mappa ancora il kernel, ma lo prepara.
4. **Metà alta condivisa solo in parte.** Vedi il contratto qui sotto: la root del processo
   non copia tutti gli slot alti.
5. **Stack.** Uno stack kernel per processo nella metà alta, con guardia: non più nella
   metà bassa come nel probe. Sotto il limite utente solo pagine utente.
6. **Stato per CPU.** `X86_CPU_LOCAL` riceve stack kernel e RSP utente salvato. È un
   cambio dell'ABI validata in K3: offset e dimensione vanno aggiornati e riprovati.
7. **Promozione nel kernel.** GDT con descrittori utente per BSP e AP, `CR4.SMEP` e
   `CR4.SMAP` su ogni CPU da CPUID. `SCE` e MSR solo sul BSP in v0. Su una CPU senza
   SMEP o SMAP il boot prosegue, il limite è nel log e il primo processo resta disabilitato.
8. **Syscall v0.** `write(ptr, len)` verso la console, `exit(code)`, errore per numero
   sconosciuto. Ritorno con `IRETQ`. Tabella errori definita in Rust. Il flag AC utente
   resta respinto.
9. **Fault utente.** Un'eccezione con CPL=3 termina il processo e registra motivo e
   indirizzo. Con CPL=0 resta panic, salvo il recupero della copia.
10. **IRQ da CPL3 nel percorso ordinario.** Il percorso oggi è ring 0. Va esteso con
    `SWAPGS` condizionale e cambio di root. Il timer sostiene un budget: un processo che
    non esce viene terminato. Niente preemption fra processi utente in v0.
11. **Un processo alla volta, sul BSP.** Più processi concorrenti e AP sono un blocco
    successivo. Qui si provano isolamento fra esecuzioni e recupero delle risorse.
12. **KPTI non abilitato.** Il vincolo di K5 resta: solo programmi nostri.

## Contratto delle due root

Stato: implementato e provato in K6-C. La tabella è confermata sul codice.

**Il problema.** Sette voci PML4 (FB, GPU, SURFACE, IOMMU, DMA, HEAP, AP) sono scritte e
azzerate a runtime nella root del kernel. Una copia in ogni processo diventerebbe stantia.

**La regola.** La root del processo contiene la metà bassa dell'utente e solo le voci alte
necessarie alla transizione. Tutto il resto del kernel è raggiungibile solo dalla root del
kernel, quindi si passa a quella prima di usarlo.

| Struttura | Dove sta | Root del processo |
| --- | --- | --- |
| Codice di ingresso e ritorno, gestori NMI e fault | Immagine, slot 511 | Sì |
| IDT, GDT, TSS del BSP | Immagine, slot 511 | Sì |
| `X86_CPU_LOCAL` | Immagine, slot 511 | Sì |
| Stack IST e stack kernel | Slot 488 | Sì |
| Slot ricorsivo 510 | - | No, i validatori usano la root del kernel |
| FB, GPU, SURFACE, IOMMU, DMA, HEAP, AP | Slot 481-487 | No |
| LAPIC | Slot 488 | Sì come effetto, non usato in transizione |
| Mappa e contratto di boot | Slot 479, 480 | No |

La tabella va confermata sul codice all'inizio di K6-C. Lo slot 511 mappa l'intera immagine:
non è isolamento, e KPTI resta una decisione separata. Lo slot 488 contiene anche la finestra
di pagina, che non si usa mai con la root del processo.

**Proprietà delle tabelle.**
- Il kernel possiede le tabelle sotto gli slot 488 e 511. La root del processo punta alle stesse
  tabelle e non le libera mai. Le modifiche sotto queste voci valgono per entrambe le root.
- Quelle due voci PML4 non cambiano dopo il boot. Il kernel lo verifica alla creazione e alla
  raccolta di ogni processo. Se cambiano: arresto.
- Il processo possiede la propria PML4 e le tabelle della metà bassa. Si liberano solo quando il
  processo è terminato e nessuna CPU ha caricato la sua root. In v0 basta il cambio sul BSP.
- Le pagine vengono azzerate prima della restituzione e il budget fisico è ricontrollato.

**Ordine all'ingresso `SYSCALL`.** `SWAPGS`, RSP utente salvato per CPU, stack kernel, frame,
registro salvato, CR3 del kernel. **Al ritorno:** frame validato, CR3 del processo, `SWAPGS`,
`IRETQ`. Lo stack resta mappato in entrambe le root. IF=0 per tutto il percorso.

**NMI.** IF=0 non lo blocca. Il gestore non presume nulla: legge CS, CR3 e la coppia di MSR di
GS. Salva CR3 e CR2, passa alla root del kernel e al GS kernel, poi rimette lo stato esatto
prima di `IRETQ`, compreso AC. Lo stesso vale per #DB e per ogni fault fatale in transizione.
Un panic da questi punti deve prima arrivare alla root del kernel.

**Copia utente.** Dentro la finestra CR3 è quello del processo e AC=1. #PF resta sull'IST: il
recupero usa solo codice e dati dell'immagine, ripristina AC=0 e la primitiva torna alla root
del kernel. Un fault fuori dalla finestra passa prima alla root del kernel e poi va in panic.

**Prove obbligatorie in K6-C.** Un NMI reale, con ripresa corretta, in ciascuna finestra:
prima di `SWAPGS`, fra `SWAPGS` e CR3, dopo CR3 all'ingresso, prima di CR3 al ritorno, fra CR3 e
`SWAPGS` al ritorno, durante la copia con root del processo e AC=1. Inoltre: accesso a uno slot
assente dalla root del processo, voce PML4 alterata fra creazione e raccolta.

## Stati del processo, non eseguibile

`Validato → Mappato → In esecuzione → Terminato (exit | fault | budget) → Raccolto`.
Ogni errore prima di `In esecuzione` rilascia ciò che ha preso. `Raccolto` azzera e
restituisce le pagine e verifica il budget fisico.

## Blocchi

| Blocco | Contenuto | Prova principale |
| --- | --- | --- |
| K6-A | Parser ELF e piano delle mappature in Rust | Test host con un ELF reale, corrotti e mutazioni |
| K6-B | Promozione: GDT, SMEP, SMAP, per CPU; processo disabilitato senza supporto | Tutti i profili esistenti, 10 boot |
| K6-C | Root, caricamento, `write`, `exit`, percorso minimo di fault e IRQ da CPL3 | Stampa ed esce; un fault termina il processo; NMI nelle finestre; pagine restituite |
| K6-D | Budget temporale e campagna completa | Programmi che sbagliano e che non escono |

Casi finali, 10 boot sul BSP: stampa ed esce, poi fault nullo, scrittura su sola lettura,
istruzione privilegiata, salto al kernel, puntatore kernel in `write`, ciclo infinito. Fra
un programma e l'altro il secondo non deve vedere i dati del primo. Alla fine budget fisico
uguale e `KERNEL_READY`. Negativi: ELF corrotti respinti prima di allocare, panic per
fault del kernel, risorse trattenute se il ripristino è incerto.

## Migrazione della build

Sostituire 136 script insieme bloccherebbe K6 e perderebbe prove già valide. Propongo una
sostituzione progressiva: ogni percorso sparisce solo dopo aver mostrato parità.

Regola da subito: nessun nuovo script PowerShell. Tutto ciò che K6 aggiunge usa Meson,
Ninja e runner Rust. K6-A e la migrazione avanzano in parallelo.

| Passo | Contenuto | Criterio per cancellare il vecchio |
| --- | --- | --- |
| M1 | Runner QEMU in Rust: VM posseduta, monitor, parser seriale, risultati | `k5-copy` e negativi con stesso esito di `qemu.py`, compresi timeout, cleanup della VM ed evidenze diagnostiche |
| M2 | Meson per kernel ordinario e K6, freestanding MSVC, MASM, rustc | Stessi sorgenti, flag e ordine di link del planner, gate del profilo |
| M3 | Campagne e suite host per gruppi, a partire da K5 | Stesso conteggio di casi e stesso esito |
| M4 | Bootloader e profili rimanenti | Immagine validata e campagne verdi |

Da verificare in M2: `meson setup --vsenv` per l'ambiente MSVC senza script, regole per
oggetti freestanding e `.asm`, invalidazione di header e asset, target Rust per gli ELF
utente, già usato da K6-A. Meson 1.12.1 è installato e K6-A lo usa con `--vsenv`.

Limiti da dichiarare:
- Meson richiede Python, salvo un pacchetto che lo include. PowerShell può sparire,
  Python resta finché non si sceglie diversamente per Meson.
- La migrazione completa non finisce dentro K6. A fine K6 restano script per i vecchi profili.
- Una prova di parità va rifatta per ogni profilo: oggi i profili diagnostici sono molti.

### M1, risultato

Stato: parità mostrata su `k5-copy` e sui sei negativi. Con l'approvazione dell'utente queste
sette campagne usano il runner Rust e `copy_boot` e `copy_negative` sono usciti da `firmware.py`.
Il tratto Python rimosso resta nella storia di git.

Il runner è `scripts/tooling/qemu-runner/`: Rust senza dipendenze esterne, test host in
`test/tooling/qemu-runner/`. `campaigns.json` lo indica come `qemu-runner` e `campaign.ps1` lo
compila con Meson prima dell'uso. Le campagne si lanciano ancora da `scripts/test/qemu.ps1`.

```bash
meson setup kernel/build/meson-qemu-runner test/tooling/qemu-runner --vsenv
```

```bash
meson test -C kernel/build/meson-qemu-runner
```

```bash
kernel/build/meson-qemu-runner/qemu_runner --image IMMAGINE --boots 10 --copy
```

```bash
kernel/build/meson-qemu-runner/qemu_runner parity CAMPAGNA_A CAMPAGNA_B
```

Le immagini e il loader si compilano ancora con `kernel/scripts/build.ps1` e
`arch/x86/boot/uefi/scripts/build.ps1 -BootKernel`: è il passo M2.

| Prova, stessa immagine e stesso loader | Python | Rust |
| --- | --- | --- |
| `k5-copy`, tre campagne da 10 boot | 30/30 | 30/30 |
| Sei negativi, 3 boot ciascuno | 18/18 | 18/18 |
| Guest senza NMI, deadline 40 s | `Missing KERNEL_READY` | uguale |
| VM ferma con `stop`, deadline 40 s | `Missing KERNEL_READY` | uguale |
| Negativo con VM ferma, deadline 60 s | `Negative copy case was not rejected` | uguale |
| VM uscita con `quit` | `Owned VM exited before KERNEL_READY` | uguale |
| Negativo con VM uscita | `Negative copy case was not rejected` | uguale |
| QEMU ucciso dall'esterno | `Owned VM exited before KERNEL_K5_NMI_ARMED` | uguale |
| Runner ucciso | VM orfana | VM terminata, evidenze conservate |
| VM estranea accesa durante le prove | intatta | intatta |

Cosa confronta `parity`, boot per boot:
- `results.json`, con hash di immagine e loader, a meno del percorso delle evidenze e dei fine riga;
- elenco dei file, hash dell'ESP, stderr;
- sequenza dei marker e valori dei campi seriali;
- arresto letto dal monitor, triple fault;
- fault in CPL3 e `#PF` del kernel nella traccia `-d int`.

Ignora solo ciò che dipende dai tempi del guest, ma ne controlla la presenza: `K5_NMI_RIP`,
tentativo di calibrazione e `TIMER_CALIBRATION_RETRY`, tempi del timer, `RIP` e flag dell'IRQ del
scheduler. Il numero di comandi al monitor varia con il carico e non viene confrontato. Nei 60
boot di `k5-copy` `K5_NMI_RIP` resta dentro la finestra attesa. Il ritentativo di calibrazione
compare in 2 boot su 48 per ciascun percorso.

Il parser sostituisce le regex a mano. Confronto con `serial.py` e le regex di `firmware.py`:
5012 log, 12 reali e il resto mutati con seme fisso, nessuna differenza. Un valore esadecimale
oltre 64 bit satura: QEMU ne stampa 16 cifre. Lo script di confronto era temporaneo e non è nel
repository, perché il riferimento Python è destinato a sparire.

Differenze volute rispetto a Python:
- Il runner sta in un Job Object con `KILL_ON_JOB_CLOSE`: se muore, muore anche la sua VM.
- I comandi al monitor passano da un thread: una QEMU che non legge stdin non blocca le deadline.
  Python invece può restare bloccato quando il pipe si riempie.
- Una VM che non termina entro 5 s diventa un fallimento registrato, non un'eccezione non gestita.
- Ogni boot aggiunge `vm.json`: PID, argomenti, guasto iniettato e modo di chiusura.
- `--inject drop|stop|quit` serve solo alle prove di guasto.
- Un negativo rilegge il log seriale dopo aver visto l'arresto. Python usava una lettura di 100 ms
  prima: poteva perdere `K5_COPY_OUTSIDE` e fallire a torto, o non vedere `KERNEL_READY` e accettare
  a torto. I test host con il falso QEMU lo hanno mostrato sotto carico. Non è mai successo nei boot
  reali provati.

Prove sul binario finale:
- da `scripts/test/qemu.ps1`, immagini ricompilate: 7/7 campagne, 28 boot;
- sulle stesse immagini delle evidenze Python: `PARITY SAME` per tutte e sette;
- test host: 10 ripetizioni in serie e 20 con quattro processi insieme, tutti verdi.

Le immagini cambiano hash se cambia la cartella di output della build, quindi `parity` va usato su
campagne eseguite con lo stesso file immagine.

Limiti:
- Il runner copre solo `k5-copy` e i negativi, con video `std` e senza `screendump` o `xp`.
- Le altre campagne usano ancora `qemu.py`, `serial.py` e il resto di `firmware.py`, che quindi
  restano fino a M3. Dopo la rimozione: smoke da 1 boot di `k5-privilege`, `k5-syscall`, `k5-ap`,
  `k5-irq-simd`, `k5-sys-rip` e `k5-ap-gs`, tutti verdi; le campagne complete non sono state rifatte.
- `sys_negative` e `irq_negative` in `firmware.py` hanno avuto la stessa correzione della lettura
  del log. Con un falso VM che stampa l'accettazione e si arresta durante l'attesa, il vecchio codice
  sbaglia 4 casi su 4 e il nuovo nessuno. Campagne complete, 3 boot ciascuna: `k5-irq-*` 5/5 e
  `k5-sys-*` 9/9. Quel falso VM è una prova temporanea, non è nel repository.
- Solo Windows. Prove su QEMU 11.1.0 TCG, q35, OVMF, una vCPU, con altre VM accese sulla macchina.
- Il criterio di M1 è la parità con Python: non aggiunge prove sul kernel oltre a quelle di K5.
  Coverage non misurata.

## K6-A, risultato

Il parser è `kernel/process/rust/elf/parser.rs`: Rust sicuro, senza allocazioni, solo `core`.
Limiti: 8 segmenti, 1024 pagine, immagine fra 0x400000 e 0x30000000. Non è ancora collegato al
kernel: l'interfaccia FFI arriva con K6-C.

Prova, senza PowerShell:

```bash
meson setup kernel/build/meson-k6-elf test/kernel/process/elf --vsenv
```

```bash
meson test -C kernel/build/meson-k6-elf
```

La fixture è un ELF reale prodotto da `rustc` per `x86_64-unknown-none` con `rust-lld`, con tre
segmenti e un `.bss`. I test girano a `opt-level` 0 e 2 con controllo overflow: ELF reale,
confini, identità e tabella corrotte, segmenti e layout respinti, ogni troncamento, ogni bit di
header e tabella, 200.000 mutazioni con seme fisso. Il codice è sicuro, quindi non serve il
sanitizer. Coverage non misurata. Nessuna prova in QEMU: K6-A non tocca il kernel.

## K6-B, risultato

Il kernel ordinario ha ora i descrittori utente in GDT e SMEP e SMAP attivi su ogni CPU che
li supporta. Il primo processo non esiste ancora: `x86_user_mode_allowed` è la condizione
che K6-C dovrà controllare. `SCE` resta spento.

**Layout.** I selettori sono 2Bh e 33h, adatti a un futuro `SYSRET`. Per tenerli contigui
la struttura `X86_KERNEL_SEGMENTS` cambia: limite GDT 55, TSS a +64, GDTR a +168, dimensione
192 byte. Il loader mantiene la propria GDT.

**Politica.** Rust calcola i bit da CPUID e decide se i processi sono ammessi. Assembly legge
CPUID e scrive CR4 con rilettura. C collega i due. Su una CPU senza SMEP o SMAP il boot
prosegue, abilita ciò che esiste e registra `USER_MODE_DISABLED`. Un AP che non ha un bit
del BSP disabilita i processi per tutti; questo caso non è provato, perché QEMU espone la stessa
CPU a tutte le vCPU.

**Prove.**

| Prova | Boot | Esito |
| --- | --- | --- |
| `k6-protection`, 4 vCPU: CR4 letto su ogni CPU | 10 | 10/10 |
| `k6-protection-no-smap`, `-no-smep`, `-none` | 3 ciascuno | 3/3 |
| `k5-*` e negativi K5 | 10 positivi, 3 negativi | tutti verdi |
| 20 positivi AP, IPI, `percpu`, `ap-runtime`, `paging-retirement` | 10 ciascuno | 20/20 |
| 19 panic attesi e negativi AP | 3 ciascuno | 19/19 |
| `kernel-boot` | 23 scenari | 23/23 |
| Tutti i 102 profili | 1 ciascuno | 101/102, poi `kernel-boot` corretto |
| Suite host | - | 58/58 |

Evidenze in `kernel/build/`: `qemu-run-2d21de2c1654483e9f0a0489b101cdf3`,
`qemu-run-3374ae565b324f69a2ecbfedb1b68062`, `qemu-run-b63516bf74084d1da6c54b78a0cbb662`,
`qemu-run-2475133611c546f2a63347c88728ae63`, `qemu-run-bd0f562e9fa54b478765bf7f59b40815`.
Passati anche i test Rust con Meson, clippy e cppcheck su `protection.c`.

**Limiti.**
- I profili VirtIO, framebuffer, console, shell, heap, sync, `k3-soak` e i restanti `percpu-*`
  hanno 1 boot, non 10: non usano il codice cambiato. La chiusura di K6 rifà l'intero set.
- Alcune campagne di panic da 10 boot sono state eseguite con 3.
- `k6-protection` è ancora un runner Python: va portato sul runner Rust di M1. È un'eccezione
  temporanea alla direzione di K6.
- Un timer non calibrato ha fatto fallire una volta `kernel-boot` (`stack-128`) con la macchina al
  71% di CPU. Non si è ripetuto. La causa non è dimostrata.

**Test corretti.** Oltre al layout, `kernel-boot` non passava da almeno il 6 novembre: non contava
le pagine AP nell'inventario, cosa che riguarda K3, non K6-B. Il selettore invalido del test di
#GP passa da 28h a 38h, perché 28h è ora valido. L'NMI via stdin del monitor viene reinviato ogni
2 secondi: il primo invio andava perso, con il kernel che gestiva correttamente quello inviato
da `qemu.py`. La causa della perdita non è stata chiarita.

## K6-C, risultato

Il profilo `k6-process` carica veri ELF nella propria root, li esegue a ring 3 e ne raccoglie le
risorse. Il kernel ordinario non esegue ancora processi: passa a farlo alla chiusura di K6-D.

| Parte | Dove |
| --- | --- |
| Piano delle pagine, range, frame, recupero, finestre NMI, stati | `kernel/process/rust/`, Rust |
| Ingresso, ritorno, fault, IRQ, NMI, copia | `arch/x86/kernel/process/assembly/entry/transition.asm` |
| Collegamento a finestra, TSS, MSR e console | `arch/x86/kernel/process/process.c` |
| Programmi di prova | `test/kernel/process/programs/program.rs`, Rust e `global_asm` |
| Scenario di prova | `scripts/tooling/qemu-runner/rust/scenario/process.rs`, Rust |

**Prove sull'immagine finale.**

| Prova | Boot | Esito |
| --- | --- | --- |
| `k6-process`: un ELF stampa ed esce, ELF corrotto respinto senza allocare, 4 fault utente terminati, puntatori cattivi con errore e copia recuperata, memoria non visibile fra due esecuzioni, IRQ del timer da CPL3, NMI nelle 6 finestre | 10 | 10/10 |
| `k6-process-disabled`, CPU senza SMAP: nessun processo, boot completo | 3 | 3/3 |
| `k6-process-kernel-fault`, fault del kernel durante una syscall: panic | 3 | 3/3 |
| `k6-process-absent-slot`, slot assente letto dalla root del processo: panic | 3 | 3/3 |
| `k6-process-root-changed`, voce PML4 cambiata fra creazione e raccolta: panic | 3 | 3/3 |
| `k6-process-no-nmi`, nessun NMI nella finestra: arresto | 3 | 3/3 |
| `k6-process-no-swapgs`, `SWAPGS` mancante: arresto | 3 | 3/3 |

Evidenze in `kernel/build/`: `qemu-run-9d1bfe167ae040909840cb5e82b38387` e
`qemu-run-a35837f296d74616a9efdd5161499307`. Le regressioni `k5-copy`, `k5-copy-rip`, `k5-syscall` e
`k5-privilege` (3 boot) sono passate prima dell'ultima modifica, che riguarda solo il codice di
`k6-process`: `qemu-run-c1346d535eab4904a4662832b66431b1`. Passati anche i test Rust con Meson, i
test del runner, clippy, cppcheck e `build-plan`.

**Differenze dal design.**
- `X86_CPU_LOCAL` non cambia. Con un processo sul BSP bastano variabili dedicate. Più CPU in ring 3
  richiederanno il campo per CPU.
- Lo stack kernel è statico nell'immagine, con canary controllato alla raccolta, senza guardia.
- Un processo ha al massimo 10 pagine di immagine, per il limite di 16 pagine della finestra, e
  sta in una sola tabella PT fra 0x400000 e 0x600000. Stack utente di 2 pagine con guardia.
- Un frame di ritorno non valido, per esempio con AC attivo, termina il processo senza fermare il kernel.

**Limiti, da chiudere in K6-D o dopo.**
- Il solo IRQ gestito da CPL3 è il timer del profilo, vettore 41h. Ogni altro vettore e ogni NMI non
  armato fermano la CPU: niente IRQ di dispositivo né panic SMP coordinato durante un processo.
- Nessun budget temporale: un ciclo infinito non viene terminato.
- Solo BSP, una vCPU, un processo alla volta, QEMU TCG. `KPTI` non abilitato: solo programmi nostri.
- Gli indirizzi di STAR, LSTAR e CSTAR non sono ricontrollati su Vol. 4.
- Il costo del cambio di root sul TLB non è misurato.
