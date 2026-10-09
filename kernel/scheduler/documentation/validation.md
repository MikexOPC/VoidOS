# Scheduler — piano di accettazione e review del progetto

Data: 8 ottobre 2026. La matrice sotto conserva il piano iniziale e non costituisce
da sola un elenco di PASS. Gli esiti realmente eseguiti sono nel
[rapporto core Rust/FFI](core.md) e nel
[rapporto finale nativo/QEMU](../../../arch/x86/kernel/scheduler/documentation/integration.md).
Perimetro: [scheduler cooperativo v0](design.md) e suo [backend x86](context.md).
Baseline del kernel invariata: `14649ba`.

## Gate host della politica e FFI

| ID | Caso da implementare | Risultato richiesto |
|---|---|---|
| S01 | Owner nuovo, run senza task | EMPTY, nessun accesso a record nativo |
| S02 | Reserve/publish, una e otto entry | READY unici, ordine FIFO verificato |
| S03 | Nono reserve con otto slot occupati | CAPACITY, stato immutato |
| S04 | Indice fuori range, zero/vecchia generazione | INVALID_HANDLE senza mutazioni |
| S05 | Abort RESERVED dopo rollback | FREE; publish/abort ripetuto rifiutato |
| S06 | Select -> YIELD -> select | RUNNING unico, reinserimento in coda |
| S07 | Select -> EXIT -> collect | FINISHED fuori coda, poi FREE |
| S08 | Collect RUNNING/READY/RESERVED | Rifiuto, stack conservato |
| S09 | Riutilizzo slot dopo collect | Nuova generazione, vecchio handle invalido |
| S10 | Generazione massima | RETIRED, nessun wrap/nuovo handle |
| S11 | Tutte le coppie stato/operazione | Transizioni consentite oppure rifiuto immutabile |
| S12 | Wrap head della coda per almeno 10.000 yield | Ordine, count e membership integri |
| S13 | Contatori al limite | Nessun wrap; errore esplicito prima di mutazione |
| S14 | Rientro gateway / accesso dal contesto errato | Rifiuto, nessun doppio prestito |
| S15 | Trace deterministiche contro modello indipendente | Stessi esiti e sequenze, seed registrati |
| S16 | Mailbox assente, motivo invalido, handle diverso | Fail-stop, nessuna raccolta o scelta successiva |

Il core Rust testa strutture safe reali, non una copia in C dell'algoritmo.
Il gateway C/Rust deve essere linkato ed esercitato insieme. ABI: interi a
larghezza fissa, nessun enum Rust implicito/bool non normalizzato fra linguaggi.
Rust -D warnings; MSVC /W4 /WX. ASan sul lato C se supportato dalla toolchain;
non dichiarare che strumenta automaticamente l'Assembly o ogni oggetto Rust.

## Risorse e casi negativi backend

| ID | Caso | Evidenza richiesta |
|---|---|---|
| B01 | Layout C e costanti Assembly | Tutti gli offset, sizeof 592, align 16 |
| B02 | Stack misallineato/fuori range/alias | Rifiuto prima del cambio RSP |
| B03 | Lease/pool insufficienti | Nessun READY, accounting al valore iniziale |
| B04 | Failure a ogni fase di mapping | Rollback delle sole risorse possedute |
| B05 | Failure durante rollback | Stato trattenuto, niente FREE o salto |
| B06 | CPU/IF/DF o stato FP fuori profilo | Rifiuto prima dello switch |
| B07 | Guard bassa e alta | Page fault previsto su immagini isolate |
| B08 | Esecuzione dallo stack | NX fault previsto, non successo ordinario |
| B09 | Stack terminato dopo revoca | Fault previsto, nessun riuso prematuro |
| B10 | Return bootstrap inatteso | Percorso terminale previsto, nessun RET arbitrario |
| B11 | Cleanup mentre task attivo | Rifiuto, lease/mapping conservate |
| B12 | Opcode AVX/AVX-512/AMX fuori profilo | Gate disassembly fallisce |

Prima del backend: fonti primarie ed edizione fissata per istruzioni/bit CPU,
profilo di indirizzi VA dimostrato senza collisioni, budget page table e TLB.
Zero magic address ricavati dalla disponibilita apparente di un indice PT.
Non basta mettere una canary per dichiarare presenti le guard o NX.

## Gate nativo QEMU, profilo finito

Workload iniziale proposto: due task, ciascuno 1.000 yield, variabili locali e
stack distinti. Questo e un carico di test, non un limite di servizio.
Un task termina prima dell'altro in una variante, uno termina subito in un'altra;
test anche zero task e un task. Non aggiungere un ciclo infinito al positivo.

La prova Assembly dedicata deve impostare pattern differenti per GPR non volatili
e XMM6-XMM15, cedere e verificarli alla ripresa senza affidare i pattern a un
compilatore C che puo riallocare i registri. Verificare controllo FP con valori
validi e contratto ABI rispettato; non variare riservati di MXCSR. GPR volatili
non sono promessi attraverso una CALL. Registrare primo ingresso e return a yield.

Dieci boot consecutivi dello stesso ELF positivo con OVMF/ESP isolata:

1. Marker di avvio, handle/generazioni, stack corrente nel record atteso.
2. Sequenza alternata e contatori esatti, variabili locali conservate.
3. RIP/RSP della ripresa, IF=0, CR3 invariato, nessun fault inatteso.
4. Guard/PTE realmente non presenti, stack RW/supervisor/NX nei readback.
5. EXIT non ripreso; raccolta soltanto sullo stack dispatcher.
6. Lease e mapping rilasciate, page table residue contabilizzate separatamente.
7. Marker SCHEDULER_DONE dopo verifiche e ripresa del kernel ordinario.

Il test host possiede il processo QEMU, impone una deadline e termina solo quella
VM; nessuna VM silenziosa e PASS. Manifest dei sorgenti e immagini per il diff
finale, log console/IRQ e snapshot CPU/memoria. Prima di installare nella ESP
ordinaria verificare hash identico all'ELF positivo provato; immagini fault
non installabili. Non riutilizzare gli 82 profili watchdog come prove scheduler.

Un test negativo di task senza yield deve scadere alla deadline host: e evidenza
del limite cooperativo, non recovery del kernel. Non chiamare il modulo adatto
a task non fidati dopo questo risultato. Dieci boot positivi non misurano SMP,
hardware fisico, fairness temporale o isolamento ring 3.

## Performance e budget

Misurare separatamente: costo policy Rust/FFI, doppio switch, log diagnostico e
mapping iniziale. Riportare CPU QEMU/acceleratore, toolchain, workload e metodo;
non inferire microsecondi dal solo elapsed host. Target di latenza N/D.

Memoria payload: 64 KiB per task; due task 128 KiB, otto 512 KiB. Con guard,
span VA per slot 72 KiB; massimo otto slot 576 KiB di span, non RAM allocata.
Contesti 5.328 byte; metadata native/Rust e page table da misurare alla build.
Boot time, throughput e copertura linee/branch N/D fino a misure reali.

## Review del progetto e condizioni prima di implementare

Scelte coperte: dispatcher/exit, ownership dei due linguaggi, nessun prestito
oltre lo switch, generazioni senza wrap, FIFO limitata, ABI SIMD, IF/DF,
rollback e rischio task non cooperante. I gate di mapping/CPU/template FP sono
requisiti di implementazione, non prove gia completate.

Il profilo cooperative e autorizzato; non serve una nuova approvazione per
realizzarne dettagli coerenti. Un cambio a preemption/SMP/user mode richiede
un nuovo progetto e discussione. Non allargare il modulo durante il debug.

Questo blocco contiene solo documentazione: build, ASan, leak e QEMU sono N/A
per il suo diff. La verifica documentale controlla fonti primarie consultate,
link locali, conteggi matematici, coerenza fra documenti e whitespace. Esiti
effettivi vanno aggiunti solo dopo averli eseguiti. Stato modulo eseguibile:
NON IMPLEMENTATO, nessuna dichiarazione READY TO PROCEED runtime.

## Esito documentale effettivo

Eseguito l'8 ottobre 2026: quattro documenti, 15 link locali risolti, zero
trailing whitespace; verificati aritmeticamente offset FP 80/allineamento 16,
dimensione 592, totale nove contesti 5.328 e frame sintetico prima/dopo RET/CALL.
Review incrociata completata su lifecycle, rollback di publish, coda, durata
FFI, limiti del profilo cooperativo e distinzione test pianificati/risultati.
Fonti primarie consultate online e linkate nei documenti. Build/leak/QEMU N/A
per questo blocco solo documentale; coverage e performance restano N/D.
