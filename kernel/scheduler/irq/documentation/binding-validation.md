# Collegamento IRQ-eventi: verifica della durata

2026-10-08. Modulo scheduler, backend x86 e test/script del modulo.
[Progetto discusso](binding-design.md). Una sorgente per evento scelta dall'utente.
Blocco implementato, corretto e verificato sulle immagini finali indicate sotto.

## Implementazione

- Registro Rust fisso, otto slot, source identity e generazione senza wrap.
  Nessun puntatore dereferenziato o heap nel registro.
- Attach rifiuta evento fuori range, source zero/duplicata, slot occupato,
  pending e generazione esaurita. Detach conserva generazione e rifiuta token
  estranei/scaduti o pending; non cancella bit di altri eventi.
- Guard non bloccante protegge i borrow Rust. Il callback di contesto precede
  il borrow; il post ISR non accede all'Owner scheduler e non chiama callback.
- Backend verifica source/Phase/Error e LAPIC idle prima del distacco. Source
  valida e stop/EOI gia completati sono prerequisiti; Rust non li sostituisce.
- Event2 del worker ed event3 delle due attese HLT usano token espliciti.
  La seconda attesa usa una generazione nuova; il backend prova il rifiuto
  dei token vecchi e dei post dopo detach. Epoche evento non azzerate.
- L'API event-only e esclusa dalla build production: rimane sotto cfg/define
  espliciti per le fixture storiche. La nuova fixture usa la libreria production.

## Debug e miglioramenti

Due boot intermedi non sono prove finali:

1. La prima versione richiedeva il native gate dopo l'unbind del mapper task.
   Il gate rifiutava correttamente. La fase finale ora verifica CPU/BSP,
   IF/DF/CR3 tramite API gia esistenti, stack principale, allocator e ambiente
   eccezioni; non richiede un mapper task ancora collegato dopo la raccolta.
2. Un controllo interpretava Active come hardware armato. Nel contratto LAPIC
   PENDING resta logicamente Active fino a consume, anche dopo stop/EOI. Il
   distacco usa il contratto reale e LAPIC idle. Il mock e stato corretto per
   conservare Active fino al consume; nuovo caso controller non quiescente
   rifiuta il distacco e mantiene binding, con failure diagnostico 6.

Log intermedi conservati: `scheduler-binding-smoke.log` e
`scheduler-binding-smoke-fixed.log` sotto kernel/build. Non sono conteggiati PASS.
Nessun nuovo accesso MMIO/I/O, bit CPU o Assembly; vengono riusate le primitive
timer/CPU gia documentate. Ricerca primaria e compromessi nel progetto linkato.

## Prove host finali

| Prova | Risultato | Limite |
| --- | --- | --- |
| Rust Owner e pending | 28/28 debug e 28/28 release | Core, modello e atomiche host |
| Rust registro | 3/3 debug e 3/3 release; 80.000 cicli bind/detach per profilo | Modello software, non device |
| Clippy e compiler | Warning come errori, PASS | Toolchain/ABI supportata |
| C/Rust regressione | 545.700 controlli, 80.000 select/yield | Context CPU mock |
| Engine | Nove profili, 69.078 controlli | Risorse/CPU mock, vero Rust |
| Nuova FFI binding | 110.016 controlli, 10.000 riusi | Registro/pending reali, CPU/hardware mock |
| Production symbols | API event-only assente dalla libreria | Ispezione dumpbin, non sicurezza contro codice kernel ostile |
| Backend | CPU, switch, stack, mapping, IRQ e ledger: dieci ripetizioni PASS | Privilegi/mapping/allocator in parte mock; switch Windows reale |
| Idle | Nove profili per dieci ripetizioni PASS; nuovo rifiuto quiescenza 29 controlli/run | Gateway/device mock |
| Cppcheck | Due fixture core e 23 input backend, zero error/warning/performance/portability | Style/information separati negli XML |

Evidenze host:

- [Core](D:/OS/kernel/build/scheduler-binding-core-final.log), directory
  `kernel/build/scheduler-host-e8c1b5496b79492bb0386857714b739f/`.
- [Backend](D:/OS/kernel/build/scheduler-binding-backend-final.log), directory
  `kernel/build/scheduler-context-2b5814819f614d0fad853c96e30584be/`.

ASan strumenta le fixture C, non il Rust staticlib o Assembly. Il registro non
alloca heap; la RAM fisica e verificata separatamente dal firmware/accounting.
Valgrind non eseguito; coverage linee/branch N/D. Nessuna promessa generica di
zero leak sull'intero OS.

## Prove firmware finali

[Immagine finale](D:/OS/kernel/build/scheduler-binding-verified/kernel.elf):
SHA256 `F9E33734E06615AD92B9C8634E3F341D5EB8FB44BA2206F23A3E49E0251C14A4`.

Dieci boot consecutivi PASS in q35/TCG/max, 128 MiB, un BSP, OVMF/ESP privata.
Tre IRQ LAPIC hardware: worker IF1 su stack task e due attese HLT IF1 su stack
principale. Verificati marker binding, contesti, CR3/RSP/RAX finali, budget
restituito e 144 PTE task assenti. [Log positivo](D:/OS/kernel/build/scheduler-binding-positive.log),
directory `kernel/build/scheduler-firmware-aa16285e6cfe4293bb68f144912eeeb5/`.

Nove regressioni negative PASS, un boot isolato ciascuna: guard-low,
guard-high, NX, retired, rollback, blocked, irq-timeout, idle-timeout e
idle-repeat-timeout. [Risultati e hash](D:/OS/kernel/build/scheduler-binding-results.json)
e [directory delle evidenze](D:/OS/kernel/build/scheduler-binding-review/negative-evidence.json).
I tre timeout verificano la retention terminale prevista, non un recupero
automatico o il rilascio di una risorsa ancora necessaria.

Loop continuo PASS: 18 eventi effettivi, stop esterno e cleanup verificati;
la soglia host richiesta era nove, lo stop asincrono ha osservato 18.
[Log loop](D:/OS/kernel/build/scheduler-binding-loop.log), directory
`kernel/build/loop-firmware-f459778f45f54a9abfdd8a42f40e67dd/`. Presente il
marker IRQ_BINDING_OK anche nell'immagine EventLoop separata.
Ogni processo VM ha deadline, acquisizione CPU/memoria e cleanup del solo
processo posseduto; fault attesi non sono crash inattesi.

## Review e limiti

63 sorgenti/script di scheduler fotografati e ricontrollati invariati con hash in
[manifest](D:/OS/kernel/build/scheduler-binding-review/sources.json).
Max sorgente 388 righe, max cinque sorgenti diretti per cartella, whitespace
e diff verificati. Snapshot dei 461 file legacy invariato: nessun file del
vecchio OS modificato, ne codice copiato in questo blocco.

Profilo BSP, task kernel fidate; IRQ ammesso nel compute Assembly e nelle
attese supervisionate esistenti. Non si certificano SMP/NMI, preemption,
user mode, payload queue, otto sorgenti hardware reali o hardware fisico.
L'identita u64 e un contratto del backend fidato, non una capability contro
codice kernel ostile. Stop e quiescenza device devono precedere unbind.

Prossimo blocco: coda dati fissa con overflow osservabile e ownership, prima
di integrare il driver di input. Il vecchio decoder sara solo un riferimento
da copiare/adattare nel nuovo modulo, senza modificare l'archivio.
