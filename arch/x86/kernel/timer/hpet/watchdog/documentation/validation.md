# R-001 — chiusura nel profilo BSP HPET/IOAPIC

7 ottobre 2026. R-001 e risolto per il profilo descritto nel
[contratto](design.md): IRQ LAPIC mancante, watchdog HPET e controller/BSP
funzionanti. Entrambi i call site timer (bootstrap finito e runtime continuo)
usano la nuova attesa indipendente. Non viene dichiarato un watchdog universale
su hardware fisico, SMP o perdita simultanea delle due sorgenti.

## Risultato verificato

- Normale: evento LAPIC consumato una sola volta; anche HPET completato/acknowledged,
  poi cleanup di token, comparator, counter, route e mapping temporanei.
- IRQ LAPIC mascherato intenzionalmente dopo arm: Count LAPIC resta zero,
  HPET sveglia HLT, risultato TIMEOUT; owner timer CLOSED, subscription azzerate,
  LAPIC fermato/ripristinato e ISR/IRR vuoti. Marker TIMER_WATCHDOG_TIMEOUT_CLEAN,
  poi panic controllato LAPIC_TIMER_INVALID. Nessun KERNEL_READY nel caso negativo.
- Stop esterno nel ciclo continuo: evento corrente consumato, StopRequest distinta
  da fault/timeout; chiusura osservata nel monitor e successivo KERNEL_READY.
- HPET assente: KERNEL_HPET_ABSENT, nessun watchdog READY e nessuna IRQ LAPIC;
  rollback e panic prima dell'attesa non protetta. Nessun fallback busy-loop implicito.

La failure injection maschera la delivery LAPIC, non arresta artificialmente il
counter HPET e non produce un IRQ mock nel kernel. Il controller, HPET, ACPI,
OVMF e backend Assembly sono quelli effettivamente eseguiti nella VM.

## Matrice finale sull'immagine corrente

Evidenze: `kernel/build/watchdog-matrix-1cc292a7d3b345b5957ed719f90d6dcc/`.
Manifest SHA-256 sorgenti/immagini, sorgenti invariati durante il test.
Profili q35, CPU max, TCG, 128 MiB, un BSP, OVMF/ESP isolata, deadline host
30 s (40 s avvio ciclo, 10 s stop GDB). Firmware BOOTX64.EFI e kernel.elf
copiati e hashati per ciascuna serie. Le fault images sono directory separate.

| Profilo | Esecuzioni | Esito |
|---|---:|---|
| Boot finito ordinario | 10 consecutive | KERNEL_READY, 3 eventi LAPIC e 3 HPET per boot, cleanup verificato |
| Boot finito con IRQ LAPIC perso | 10 consecutive | 0 eventi LAPIC, 1 HPET, timeout e cleanup, panic previsto |
| Ciclo continuo, stop dopo almeno 16 eventi | 10 consecutive | 171 LAPIC e 171 HPET complessivi, consumo completo, stop e cleanup |
| Ciclo continuo con IRQ LAPIC perso | 10 consecutive | timeout alla prima attesa, cleanup e panic previsto |
| q35,hpet=off | 2 | rifiuto prima dell'attesa timer, panic previsto |

Totale finale: **20 boot positivi, 20 injection lost-LAPIC, 2 profili assenti**.
Zero crash inattesi, triple fault o hang osservati fino ai marker/deadline del
test. I panic negativi sono risultati attesi, non vengono conteggiati come boot
positivi. Nessuna percentuale di coverage linee/branch misurata: **N/D**.

Serie raw:

- Positivi finiti: `kernel/build/watchdog-normal-3adcd1c7389c4c2a9d099b932360838c/`.
- Lost finiti: `kernel/build/watchdog-lost-c741e4feaa0a4f93abc4236f7f78df6b/`.
- Lost runtime: `kernel/build/watchdog-lost-b7781d457a754173bff8675a5e46e1d5/`.
- Assenti: `kernel/build/watchdog-absent-739dd922ff594bccbb20a4528f9115f8/`.
- Dieci serie runtime: directory elencate in `continuous-runs.json` nella matrice;
  ciascuna contiene serial.log, qemu.log, registers.log e stderr.log.

L'immagine ordinaria verificata e copiata in `kernel/build/kernel.elf` e nella
ESP di sviluppo; SHA-256 **B6E097095198B4F0F7A8EACBFA77DBA49F76BA3CFBE547812474172B11A50056**.
`installed-images.json` verifica la corrispondenza con l'ELF della matrice.
La pulizia finale dell'allineamento header ha richiesto una ricompilazione:
poiche l'ELF ricompilato differiva, e stata rieseguita la matrice completa sopra
sull'immagine finale. Nessuna prova dell'immagine precedente sostituisce questi gate.

Il checker legge realmente gli owner statici (ABI verificata da static_assert),
non soltanto messaggi OK: Count/Error, Self, token, Armed/Acquired/RouteOwned/Ready,
Result/Closed; owner LAPIC, consumo, calibrazione raw, LVT/divider/count.
Verifica IF terminale zero, ISR/IRR vuoti, opcodes STI/HLT/CLI nell'immagine e
IRQ0x41/IRQ0x40 durante il sonno con IF=1. IRQ UART0x40 precedenti sono distinti
tramite RIP del backend sleep. Le scritture HPET/IOAPIC e i ripristini sono
confermati dal codice con readback; il monitor non pretende di esporre MMIO
LAPIC come normale RAM. Questi controlli non sostituiscono una prova fisica.

## Test host e regression

| Suite | Risultato | Evidenze |
|---|---|---|
| Watchdog C + ASan | 100 ripetizioni, 10.000 attese positive piu scenari di failure/rollback | `kernel/build/watchdog-host-f92e14c8799a4e11b4609bc1afe705ab/` |
| Watchdog Rust | 3 test: conversione/limiti, wrap/margine, EVENT/TIMEOUT/STOP/FAULT | stessa directory |
| Adattatore MMIO reale C + ASan | limiti di pagina/ultimo timer, RO/write whitelist, acquisizione, prestito revocato, null e offset invalidi | stessa directory |
| HPET decoder/discovery/adapter/probe C + ASan | 100 ripetizioni per suite, inclusa preflight con inventory LAPIC live e alias RAM/lease/table/leaf | `kernel/build/hpet-description-beaebe055f4b4c32aa69d5756de017ed/` |
| HPET Rust | 6 test, tutti passati | stessa directory |
| HPET fixture UEFI C/Rust | 10 boot consecutivi, marker espliciti | stessa directory; prova componente, non watchdog |
| LAPIC C + ASan / rejection Assembly host | 10 ripetizioni, 10.000 eventi e rifiuti non privilegiati | `kernel/build/lapic-asan-7636953c24fa433da01729c63eada60d/` |
| LAPIC Rust | 9 test calibrazione/consensus/stati/wait/loop, tutti passati | `kernel/build/watchdog-lapic-host.log` |

Watchdog host usa il vero core/device/route/wait C e policy Rust; CPU, mapping,
registry e MMIO sono modellati. Un runner separato compila l'adattatore MMIO
reale con storage RAM: verifica bounds/whitelist, non W1C o ordering hardware.
Il bridge kernel e la semantica MMIO sono coperti dalle prove native.
L'ASan strumenta i sorgenti C host,
non il Rust o il kernel freestanding. Nessun uso Valgrind su RAM fisica dichiarato.

Scenari C: doppio open/close, input null/alias/goal invalido/Error, hardware ID e
periodo errati, block/timer attivi, pending iniziale, route assente/occupata,
ISO riservata, piu controller rifiutati, bind/unbind falliti, counter fermo,
arm troppo tardivo, target con wrap, Count saturato, stop con IRQ mancante,
scritture IOAPIC parziali e rollback, restore HPET fallito e retry di close.
W1C/EOI falliti mantengono owner/token/risorse live e rifiutano close; nessun
handler viene dichiarato distaccato senza quiescenza.

Nessuna nuova allocazione nel watchdog. Budget fisico e page-window idle sono
controllati prima/dopo i prestiti e la chiusura; registry ha storage statico.
Nessun leak host rilevato da questo schema di accounting; non e una misura di
leak RAM fisica universale, ne un stress SMP.

## Review e strumenti

Build C11 MSVC /W4 /WX, Rust -D warnings/panic=abort, Assembly MASM /W3 /WX
per i quattro profili: riuscita senza warning. Kernel/main.c **399 righe**, driver
LAPIC **248**, runtime **218**; nuovi sorgenti watchdog tutti <200 righe, test
platform196. Cartelle funzionali watchdog max5 file diretti, scripts4; LAPIC
root4 dopo estrazione runtime/. Nessun sorgente modificato >400 righe, nessuna
cartella del perimetro >5, trailing whitespace/tab indent/TODO/HACK assenti.
Grafo delle include locali: 42 file esaminati, zero cicli.

Cppcheck 2.22.0, sei TU watchdog, win64/C11, exhaustive/inconclusive:
**0 error, 0 warning, 0 performance**. Output `kernel/build/watchdog-cppcheck-final.xml`.
Sette style residue: due callback void* conformi alla firma di borrow e cinque
entry pubbliche identificate unused/static perche il sottoinsieme analizzato non
include il caller LAPIC/main. Sono usate dalle build reali; non sono state rese
static artificialmente. Cinque informazioni: header standard non forniti e
checkers report. Non si presenta questo scan limitato come analisi totale dell'OS.

Review manuale: bounds e wrapping, ownership prima delle mutazioni, unbind solo
dopo quiescenza, W1C source prima di EOI, mapping UC/NX revocato prima di HLT,
assenza di allocazioni/handler temporanei, errori distinti da stop/evento.
Le modifiche preesistenti della working tree sono conservate, inclusa la riga
vuota inserita dall'utente in main.c. I report full review del 5 ottobre restano
snapshot storici; la checklist corrente rimanda a questa chiusura.

Durante debug i primi checker hanno rifiutato snapshot validi per un literal
PowerShell signed e un marker panic con case errato; il checker runtime aveva
ancora l'ipotesi un solo wake per evento, corretta a 1..2 dopo HPET. Questi run
falliti sono conservati e non sommati alla matrice finale. Le serie precedenti
al diff finale e la calibrazione storica sotto carico non sostituiscono i gate
finali qui riportati.

## Limiti e prossimi gate

- Deadline v0 100 ms: ritorno ritardato anche se LAPIC arriva a circa10 ms.
  Non pronto a imporre timeslice scheduler o benchmark real-time.
- HPET esclusivo e fermo all'acquisizione, single IOAPIC24/BSP; niente fallback
  automatico, FSB/MSI, multi-owner, NMI watchdog o AP avviati.
- Guasto contemporaneo HPET/controller/CPU o IF esternamente bloccato puo ancora
  impedire il wake. Il modulo non promette disponibilita contro questa classe.
- Stop contemporaneo a IRQ LAPIC perso e prove di recovery hardware corrotto
  sono test host con modello; la stop nativa usa il timer ordinario e GDB.
- Hardware reale, sospensione/resume, firmware/SMM ostile, drift e lungo periodo
  restano da verificare. SMEP/SMAP/KPTI non vengono dedotti da questi test.

R-001 non richiede altre implementazioni nel suo profilo iniziale. R-002
(autenticazione immagini/policy) e gli altri rilievi non sono chiusi da questo
watchdog; prima di nuove funzionalita decidere la prossima task distinta.
