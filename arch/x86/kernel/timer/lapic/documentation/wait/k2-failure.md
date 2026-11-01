# Panic intermittente emerso nella campagna K2

27 ottobre 2026. Estensione di diagnosi e fix timer autorizzata dall'utente.
Stato: misura corretta e verificata; la condizione del panic originale
non è stata catturata e resta non attribuita.

## Evidenza iniziale

`kernel/build/sync-firmware-43a26d629ad44d1498c1e4442db879d7/boot-10/serial.log`:
probe sincronizzazione completato, IRQ scheduler e idle verificati, poi
TIMER_DEADLINES_FAILED e LAPIC_TIMER_INVALID. Campagna positiva 9/10;
il risultato fallito resta conservato, non trasformato in PASS.

Watchdog failure/cleanup=0, result=EVENT, cleanup riuscito. L'ultimo
marker scheduler è SCHEDULER_IDLE_OK. Il vecchio messaggio aggregava
condizioni diverse: non permette di scegliere fra quiet, accounting,
consumo, sleep rejection o misura del tempo. Nessun fault CPU riportato.

## Diagnostica aggiunta

`driver.c` registra Goal, Step, Observed, Count, Phase e Consumed prima
di tornare al cleanup già esistente. Step:

| Valore | Condizione |
|---|---|
| 1 | readiness o quiet all'inizio dell'iterazione |
| 2 | sessione diagnostica scheduler |
| 3 | arm |
| 4 | risultato wait, conteggio, errore o LAPIC idle |
| 5 | riarmo rifiutato, consume o doppio consume |
| 6 | controllo della consegna già osservata, senza nuovo HLT |
| 7 | misura PM fuori dalla finestra diagnostica |

Lo step 7 riporta i tick effettivi. Nessuna soglia, deadline, recovery,
istruzione privilegiata o sequenza di registri è stata modificata.
Se fallisce sleep_rejections fuori da deadlines resta il messaggio
aggregato: non attribuirlo automaticamente a uno degli step elencati.

## Verifiche e limite

- Host kernel: suite passata, inclusi C LAPIC e 9 test Rust; evidenza
  `kernel/build/test-run-679c9554be4e488e988ede1300f3a6b6/`.
- C ASan: dieci ripetizioni e 10.000 cicli eventi;
  `kernel/build/lapic-asan-ec23ae7e439d468ea415c3b3f7fa6add/`.
- QEMU nuova diagnostica: 10/10, poi 20/20 sulla stessa immagine,
  rispettivamente `sync-firmware-0e64e7ef8e2f4a2c846bf2e7610da199/` e
  `sync-firmware-08b09deaae29459ca2ec81beba1a9b51/` sotto kernel/build.

Il difetto non è ricomparso. Non è dimostrato che la diagnostica lo
corregga; non si allargano soglie o si abilita un retry per ottenere verde.
Non attestare modulo privo di flaky test né avanzare alla memoria sulla
sola base delle campagne riuscite. I mock host non misurano la consegna
IRQ reale o l'effetto della deschedulazione di QEMU.

## Ripresa: misura da separare prima di attribuire il panic

Altri 20/20 boot della stessa immagine passano anche durante un carico
host finito di 50 processi di contesa con otto thread. Evidenza:
`kernel/build/sync-firmware-a83ba4576c6645bc9ec4ddbe181c8dc6/` e
`kernel/build/k2-timer-resume-host-load.json`. Il carico host non dimostra
quale risorsa fosse contesa nel boot fallito originale.

Riscontro nel codice precedente: le misure idle partivano prima dell'arm LAPIC e
terminano dopo x86_watchdog_wait. Tale funzione attende anche l'IRQ HPET
indipendente da 100 ms per drenare la source. record_deadline applica a
questo intervallo complessivo il limite diagnostico circa 5..200 ms.
Il valore quindi non isola la latenza LAPIC. È un problema della misura
dimostrato dal percorso delle chiamate, non la causa già provata del panic.

### Contratto approvato dall'utente e implementato

Separare due osservazioni: arm→handler LAPIC e durata dell'attesa protetta
dal watchdog. Il driver possiede un timestamp PM per la singola source
ARMED; lo registra prima della sequenza LVT/Initial Count e misura l'elapsed
nel proprio handler, prima di notificare lo scheduler. Il campo deve
essere validato e invalidato nelle transizioni fallite/consumate. Nessun
puntatore preso in prestito sopravvive all'handler; nessuna allocazione o
stampa in IRQ. Il prefisso Count/Error usato dall'Assembly resta invariato.

Le tre misure diagnostiche vengono pubblicate soltanto dopo consegna
valida e non sovrascritte dai chiamanti scheduler dopo il watchdog.
Restano acknowledgement HPET, source stopped, EOI, contatori, timeout e
cleanup attuali. Non si allarga il limite 5..200 ms della misura LAPIC;
il tempo del watchdog viene riportato separatamente. Non si pretende che
la misura rappresenti il solo hardware: include il ritardo di consegna
e i controlli fino al punto osservato nell'handler.

Alternativa: conservare la misura end-to-end e il comportamento attuale,
continuando la sola cattura del failure. Trasformare ritardi in WARN
sarebbe una scelta distinta, non inclusa in questa proposta.

Accettazione: mock con IRQ puntuale ma ritorno del watchdog ritardato
non altera l'elapsed IRQ; IRQ tardivo resta diagnosticabile; wrap 24/32,
source ferma, errore arm/ack e consume verificati. Suite C/Rust, ASan e
dieci boot finali, con diagnostica che distingue entrambi gli intervalli.
Il panic originale resta non attribuito finché non si acquisisce la
condizione specifica; non dichiarare dimostrata retroattivamente la causa.

Perimetro concordato: solo timer LAPIC, i due chiamanti diagnostici
scheduler idle/IRQ, relativi build/test e report. Nessun cambiamento a
lock, heap, allocatore, policy HPET o scheduler generale.

Prossima indagine: conservare l'immagine con la diagnostica e catturare
la condizione precisa alla ricorrenza; confrontare separatamente il
percorso ordinario prima di attribuire causalità al probe K2. Verificare
poi la condizione con una regressione mirata e dieci boot finali.

## Implementazione e regressioni della misura

Owner da 144 a 160 byte: ArmedPm e WaitPm[3] aggiunti in coda;
offset Count=0/Error=4 e campi precedenti invariati. Timestamp limitato
alla maschera PM e ammesso soltanto in ARMED; zero è valido. Nel driver
l'osservazione avviene dopo verifica owner/ambiente, prima dei readback
LAPIC. Dopo stop/EOI riusciti viene pubblicato DeadlinePm e invalidato
ArmedPm, prima della notifica scheduler. Non è l'istante elettrico IRQ.
Errore I/O/EOI non pubblica la misura; stop incompleto conserva lo stato
armato, stop confermato/uscita richiesta invalidano il timestamp.

Limite diagnostico identico, ora verificato in Rust: 17.897..715.900 tick
inclusi, testati entrambi i confini e i valori esterni. Il tempo HPET non
viene sottratto stimandolo, ma osservato separatamente in WaitPm.

Fonte: [ACPI 6.5 §4.8.3.3, tabella 4.14](https://uefi.org/sites/default/files/resources/ACPI_Spec_6_5_Aug29.pdf).
PM_TMR RO, accesso dword; bit 23:0 base, bit 31:24 se esteso nella FADT.
Porta e maschera provengono dalla discovery esistente. Nessuna porta
fissa introdotta nel driver, né modifica a registri/timeout/routing HPET.

Regressione mock: IRQ puntuale, poi ritardo simulato di un secondo prima
di osservare il ritorno. L'intervallo complessivo fallisce il vecchio
controllo; DeadlinePm resta valido e immutato. Un IRQ davvero tardivo
resta fuori soglia. Coperti wrap 24/32, timestamp zero, doppio service,
consume/riarmo, arm fallito con stop riuscito/incompleto e metadati
temporali alterati. Non sono sospensioni forzate di QEMU.

## Evidenze della correzione

| Prova | Esito | Sotto kernel/build/ |
|---|---|---|
| Kernel host, Rust LAPIC | PASS, 10 test Rust LAPIC | test-run-d32032b258934e528583b73e24ee25f1/ |
| Adattatori x86 scheduler | PASS | test-run-b4232534ac684bb0be0dc43943236230/ |
| C finale ASan | PASS, 10 ripetizioni e 10.000 cicli eventi | lapic-asan-e8e804e8518d4d879582aeb7f2bda0d6/ |
| Clippy, cppcheck | PASS, nessuna diagnostica bloccante | k2-timer-clippy.rmeta; k2-timer-separated-cppcheck.xml |
| QEMU probe K2 | 10/10 | sync-firmware-c4778d3492f74fc882c50c9ddf398efd/ |
| QEMU ordinario finale | 10/10 | sync-firmware-1527d6936e0b4e12b3707029de0ad48b/ |
| Ciclo continuo, stop esterno | PASS, 9 eventi e cleanup | loop-firmware-ad829196cdaa419381a5c7719d17e1b4/ |
| IRQ LAPIC perso intenzionale | PASS negativo, timeout indipendente e cleanup | watchdog-lost-d8a31cdf6d70415b8284af2d702fd6c0/ |

Il primo positivo precede la sola aggiunta di WaitPm nel ramo continuo
step: la versione successiva è coperta dai dieci boot ordinari e dal
collaudo continuo. Immagini e hash dei report restano distinti. ASan
finale copre anche i test arm aggiunti dopo la suite host generale.
Un warning signed/unsigned nel nuovo test è stato rifiutato da /WX e
corretto; il risultato fallito resta conservato.

Nei 60 campioni guest: IRQ 10,399..17,350 ms, attesa 100,800..108,610 ms.
Sono osservazioni QEMU TCG, non garanzie né valori del boot originale
fallito. ASan strumenta C host; Rust collaudato separatamente. Nessuna
allocazione nuova nel timer, Valgrind non eseguito, coverage N/D,
nessun SMP guest o hardware fisico attestato.

Errore di misura riprodotto e corretto, controlli di consegna/ownership/
timeout mantenuti. Il vecchio panic è compatibile con il problema ma
non retroattivamente attribuito: manca il suo Goal/Step/Observed.
