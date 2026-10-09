# Dispatcher idle supervisionato: implementazione e validazione

Snapshot storico del blocco idle. Il successivo arresto cooperativo e le prove
aggiornate sono nel [report task persistenti](persistent-validation.md).

8 ottobre 2026. [Contratto approvato e fonti](design.md). Il perimetro e lo
scheduler BSP cooperativo con i soli agganci contabili e timer necessari.
Le prove IRQ/task precedenti restano [storiche](../../irq/documentation/validation.md).

## Risultato

SELECT distingue EMPTY da BLOCKED. Nel profilo idle due task si sospendono
sull'evento 3 e la coda READY diventa vuota. Sullo stack principale e con IF=0,
il dispatcher verifica le lease, arma la seconda scadenza LAPIC e riutilizza
l'attesa HPET con backend STI/HLT/CLI gia verificato. Non mantiene un borrow
dell'Owner Rust durante l'attesa.

L'IRQ LAPIC valida la source, completa stop/EOI e pubblica una notifica Rust
atomica. Dopo acknowledgement dell'HPET indipendente, il dispatcher ricontrolla
Count/Phase e applica una transazione Rust: due WAITING diventano READY.
Entrambe le task ricontrollano la condizione persistente e terminano; quattro
dispatch effettivi, due raccolte, budget ripristinato e 144 PTE task assenti.
L'attesa non viene conteggiata come esecuzione di una task.

La prima scadenza conserva la prova IRQ nel worker e la terza conserva il
percorso timer precedente. Totale tre consegne LAPIC e tre watchdog HPET:
il driver consuma la seconda consegna dopo la sessione idle, senza eseguire
una seconda attesa HPET per lo stesso evento. Il ciclo continuo usa lo stesso
aggancio sulla sua seconda consegna.

## Review e correzioni

La prima prova QEMU ha rifiutato l'attesa: `x86_watchdog_valid` richiedeva
FreePages uguale al budget originale, incompatibile con gli stack WAITING.
La fase SCHEDULER_IDLE_FAILURE rende il rifiuto localizzabile, anziche confonderlo
con un hang. Quel tentativo e una prova fallita, non uno dei boot finali PASS.

L'integrazione contabile conserva il budget originale del watchdog e la sua
ABI di 224 byte. Un prestito privato conserva copie bounded di due lease da
sedici pagine, disgiunte, con identita/generazioni riconosciute dall'allocatore
e pin attivi; il delta libero deve essere esattamente 32. Le verifiche restano
attive nei gate watchdog. Non si imposta un nuovo budget per accettare una
perdita e non si accetta una sottrazione arbitraria.

End termina il prestito prima della raccolta degli stack: il gate rigoroso
originale rifiuta ulteriori operazioni device finche le pagine non sono state
restituite. Close e vietato con prestito vivo. Su timeout si conservano scope,
lease e binding nel profilo terminale. La ledger privata occupa 72 byte, non
alloca heap e non prende ownership delle pagine; il protocollo e BSP/IF=0 con
caller kernel fidato, non un'API per user pointer o allocatori concorrenti.

Il reporting numerico e stato separato in `integration/report.c` per tenere
il controller sotto 400 righe. Nessun nuovo Assembly, registro MMIO o porta
hardware: sono riutilizzati i driver e i relativi prerequisiti gia validati.

## Prove host

| Prova | Esito reale | Limite |
| --- | --- | --- |
| Rust debug/release | 22/22 per profilo, Clippy e warning gate passano | Core e pending esistenti riutilizzati |
| FFI C/Rust ASan | 520699 controlli; 1000 nuovi cicli con due WAITING, doppio post coalescente, wake-all, raccolta e EMPTY | Contesto nativo mock; Rust/ASM non ASan |
| Idle adapter ASan | Cinque scenari × 10; 26/18/21/23/22 controlli per scenario | Hardware/gateway mock: successo, timeout, errore source, post rifiutato, wake count errato |
| Prestito contabile ASan | 190002 controlli × 10; 10000 scope/run | Allocatore/CPU mock, pin, drift budget, rollback, mutazione caller, overlap, end/rifiuti |
| CPU/context | 55900 controlli × 10 | Snapshot privilegiati mock; rifiuto CPL3 reale |
| Switch host | 4004 switch × 10, quattro guard fault attesi/run, due reservation restituite/run | GPR/XMM/FP reali; separato da ASan fibers |
| Stack resources ASan | 361000 controlli × 10 | Allocatore/PTE mock, lifecycle e rollback |
| Native mapper ASan | 554149 controlli × 10 | CPU/PTE mock, bounds/ownership/invalidazione |
| Ponte IRQ ASan | 22 controlli × 10 | Backend/source mock |
| Watchdog regressione ASan | 100 ripetizioni, 10000 wait positivi piu fault/rollback; compatibility 10 scenari × 100; diagnostics 24 × 100; recovery 23 × 100 | Hardware mock; adapter MMIO RAM strumentato, non un HPET fisico |
| Cppcheck | 18 input scheduler e cinque input interfacce, zero error/warning/performance/portability | Style/information conservati nell'XML |

MSVC/MASM/Rust con warning come errori, ABI Microsoft x64; nessun warning
ignorato per ottenere il PASS. Evidenze sotto `kernel/build/`:

- `scheduler-host-02308ab0630d48a885715af884bdc032/`: core, FFI, Clippy e modelli.
- `scheduler-context-7b3f20e4466d4b74bc023e8c0a8a92fc/`: backend, adapter idle,
  prestito contabile, disassembly e cppcheck.
- `watchdog-host-58b411bce9ca4af7adc21c5bae029272/`: regressione HPET.
- `scheduler-idle-interface-review/`: cppcheck interfacce, righe e manifest finale.

## Firmware finale

Immagine: `kernel/build/scheduler-idle-final/kernel.elf`.
SHA256: `96D32082BF37F14C6358468271EB9D6F1256CD37D24ECD3C81C2E41DE20293D7`.
Dieci boot consecutivi PASS:
`kernel/build/scheduler-firmware-badd796b993b438c989dda8760e80e93/`.

QEMU q35/TCG, max, 128 MiB, un BSP, OVMF/ESP privati e immagine copiata con
hash controllato. Il verifier richiede IDLE_OK, quattro dispatch, entrambe le
task risvegliate, attesa bounded, budget prima/dopo uguale e marker IRQ/task
precedenti. Confronta la prima consegna hardware con il RIP compute worker e
la seconda con il punto di ritorno HLT/CLI del dispatcher; IF del frame e 1.
Una verifica delle dieci trace controlla anche il RSP idle nel range principale:
`idle-hardware-evidence.json`. Al termine CR3 invariato, RSP principale,
RAX ready, 144 PTE task assenti e nessun triple fault. I gate driver verificano
consume/stop/unbind, quiescenza LAPIC e chiusura HPET col budget originale.

Il negativo finale idle-timeout e PASS:
`kernel/build/scheduler-firmware-4cbd99c42c0f494788793b0ba9b2daab/`.
La source LAPIC viene fermata tramite la sua API verificata prima di HLT;
l'IRQ HPET riprende realmente la CPU. La classificazione e TIMEOUT/DEADLINE,
il marker terminale e distinto da READY, IF torna a zero, due lease/32 pagine
dati restano pinnate e le guardie assenti. Non e una raccolta riuscita o una
prova di recovery del device: il dispatcher termina conservando le risorse.

La matrice finale comprende otto scenari negativi, ciascuno con immagine
dedicata compilata dai sorgenti finali e un avvio verificato. I fault previsti
sono distinti dai crash inattesi; nessuno di questi avvii sostituisce i dieci
boot positivi consecutivi della stessa immagine.

| Scenario | Esito | Directory evidenze sotto kernel/build/ |
| --- | --- | --- |
| guard-low | PASS | scheduler-firmware-539f4365be2a48f48af894ce66107a0f/ |
| guard-high | PASS | scheduler-firmware-00dae40773194d4c92850798c74c0b37/ |
| nx | PASS | scheduler-firmware-06cb6f2e1fbb4b52bd5bdb0b0135e6c4/ |
| retired | PASS | scheduler-firmware-1ac43879d5a94e76bec8b7e88ba6b454/ |
| rollback | PASS | scheduler-firmware-42d1a6ee45224e359e0692980a245c10/ |
| blocked | PASS | scheduler-firmware-5c761df0a8e84f88950bffd494845a07/ |
| irq-timeout | PASS | scheduler-firmware-4295503d2a12485b9edb4d4c276e2034/ |
| idle-timeout | PASS | scheduler-firmware-4cbd99c42c0f494788793b0ba9b2daab/ |

La regressione del ciclo continuo e PASS con nove eventi hardware, stop esterno
e cleanup verificati: `kernel/build/loop-firmware-7f251649d037423ebb15aa07d078c71d/`,
immagine `kernel/build/scheduler-idle-loop/kernel.elf`, log
`kernel/build/scheduler-idle-loop-qemu.log`. La prima consegna interrompe il
compute task, la seconda riprende l'idle e le successive riprendono il percorso
HLT del timer. Il verifier controlla conteggi, arresto/quiescenza LAPIC e
chiusura HPET. Questa prova finita non dimostra uno stress di durata arbitraria.

Totale finale: dieci boot positivi, otto negativi e una prova del ciclo continuo
su varianti dedicate. Il blocco v0 e concluso nel perimetro dichiarato. Nessuna
modifica C/Rust/Assembly dopo queste prove; manifest dei sorgenti e hash
dell'immagine positiva ricontrollati durante la review finale.

## Qualita e limiti

Massimo sorgente scheduler: 382 righe; massimo cinque file diretti nelle
cartelle funzionali. Header compatti, C alla radice funzionale, test in test/,
Rust state/pending nella struttura gia adottata. Nessun uso di reuse/ in questo
blocco; .gitignore e address_ranges.c preesistenti non sono stati modificati.

ASan host non certifica leak fisici: il positivo verifica restituzione delle
lease dei due stack e assenza PTE; il negativo conserva intenzionalmente gli
owner. Valgrind non eseguito, coverage linee/branch N/D, hardware reale N/D.
Le atomiche riguardano notifiche; non sono una dimostrazione di SMP/payload
condivisi. Nessuna nuova preemption, user mode o protezione CPU opzionale.

Il profilo e finito e monouso: evento 3, due waiter, seconda consegna LAPIC.
Non e ancora un idle generale per driver arbitrari o task persistenti che
aspettano all'infinito. L'HPET one-shot viene drenato prima della classificazione:
la latenza v0 include la sua deadline, non e un benchmark di risveglio immediato.
Perdita simultanea di LAPIC e HPET/controller resta fuori dal modello verificato.
Recovery/restart dopo il timeout terminale, piu source concorrenti e stress su
hardware fisico sono lavoro futuro e non sono dichiarati PASS.
