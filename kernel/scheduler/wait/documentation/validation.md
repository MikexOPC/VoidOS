# Attesa e risveglio delle task: implementazione e prove

8 ottobre 2026. Estensione del modulo scheduler, scelta dall'utente:
eventi statici con epoca e risveglio di tutti i waiter. Contratto e fonti:
[design](design.md). Le evidenze qui riportate riguardano BSP, task kernel
fidati e IF=0, con carico diagnostico finito.

La successiva integrazione di un IRQ LAPIC durante una task e documentata
separatamente nel [report IRQ/task](../../irq/documentation/validation.md).
Le prove di questa pagina restano la fotografia del precedente profilo IF=0.

## Cosa e cambiato

Il core Rust aggiunge WAITING mantenendo i numeri degli stati precedenti.
Otto eventi conservano un'epoca u64; ogni waiter mantiene evento/epoca nella
stessa ownership transazionale di stati, coda e generazioni. Nessun heap,
raw pointer o callback nel core. I metadata Owner passano da 208 a 464 byte,
misurati sulla toolchain x64. Il limite locale verificato rimane 512 byte.

- Observe legge l'epoca del relativo evento senza mutazioni.
- Wait confronta il token e registra WAITING nella stessa transazione. Un
  token precedente rimette il task READY; un token futuro e respinto.
- Signal incrementa l'epoca, anche senza waiter, e accoda tutti i waiter
  corrispondenti in ordine crescente di indice. Il produttore resta RUNNING
  fino al successivo yield. Un secondo signal non duplica la membership.
- CounterExhausted impedisce wrap sia dell'epoca sia del contatore diagnostico;
  errori lasciano invariati stati, associazioni, epoche e coda.
- Select senza READY restituisce BLOCKED se esistono waiter, EMPTY altrimenti.
  Collect/abort/publish di WAITING sono respinti senza liberare risorse.

Il nuovo gateway ha una risposta repr(C) da 48 byte: risposta scheduler32,
epoca8, esito4, numero waiter risvegliati4. Assert C/Rust verificano dimensione,
allineamento e offset. La generazione del task resta distinta dall'epoca evento.
Il gateway riusa il guard esclusivo; callback di contesto prima del prestito,
nessun prestito attraversa lo switch. Risposte di errore normalizzate e
comandi invalidi, contesto vietato e rientro sono coperti dalle fixture.

Nel backend C le task richiedono observe/wait/signal tramite mailbox e switch
sul dispatcher. Le risposte sono conservate **per slot**, con handle e
generazione verificati dopo la ripresa. Un waiter conserva il proprio stack
fisico pinnato e il contesto; il cleanup rimane esclusivo dei FINISHED dopo
il ritorno allo stack principale. Il punto d'ingresso in kernel main non cambia.

## Review e debug

La prima compilazione con Clippy ha rilevato un re-export Rust non usato nel
gateway. Il tipo e stato utilizzato esplicitamente nel risultato della FFI;
nessun warning e stato disabilitato.

Durante la review, prima del primo boot, e stata corretta una risposta evento
inizialmente globale: un altro task avrebbe potuto sovrascriverla mentre il
richiedente era sospeso. Ora ogni slot ha storage separato e validazione di
indice/generazione. Il workload reale alterna proprio questi due richiedenti.

Il carico conserva le cento cessioni e i canary locali per ciascun task del v0,
poi verifica due sequenze: segnale tra observe e wait (nessuna sospensione
indebita), e wait registrato seguito da signal (una sospensione reale).
Alla fine sono richiesti: un WAITING, un CHANGED, due SIGNAL, due task raccolti,
210 dispatch, 420 switch reali e budget fisico uguale a quello iniziale.
La ripresa verifica ancora contesti task/dispatcher, RSP, lease e codice RX.

## Evidenze host finali

Percorsi relativi a `D:/OS`; gli artefatti sono esclusi dai sorgenti.

| Prova | Risultato | Directory |
|---|---|---|
| Rust debug/release | **19/19 per profilo**, Clippy senza warning | `kernel/build/scheduler-host-372546e1b8474882865fb5dfc7ec453f/` |
| Modello eventi indipendente | 32 trace, 4.096 lifecycle, 217.088 comandi per profilo | stessa directory |
| Modello lifecycle v0 | 64 seed, 320.000 comandi per profilo | stessa directory |
| ABI/gateway C e Rust | 486.677 verifiche, 80.000 coppie select/yield, C con ASan | stessa directory |
| Backend CPU/contesto | 55.800 verifiche per run, dieci run | `kernel/build/scheduler-context-26c52c59f63f416c99835d7ed83d3c61/` |
| Switch host reale | 4.004 switch per run, dieci run, quattro fault guardie per run | stessa directory |
| Risorse e rollback con mock | 361.000 verifiche per run, dieci run | stessa directory |
| Mapper/permessi con backing host | 554.149 verifiche per run, dieci run | stessa directory |

I test degli eventi includono sette waiter e un produttore, otto waiter senza
produttore, evento diverso, signal ripetuti, limite epoca/contatore, token futuro,
handle vecchio, associazioni corrotte e immutabilita dei rifiuti. Il modello
indipendente usa associazioni opzionali e una deque dinamica, non la coda
circolare transazionale della produzione. Il lifecycle C finale verifica
BLOCKED e l'assenza di raccolta prematura di due waiter.

MSVC/MASM/Rust: warning come errori. Cppcheck: zero error/warning/performance/
portability; backend 14 style/20 information, fixture FFI 2 style/5 information.
I dettagli rimangono nei rispettivi `cppcheck.xml`, senza soppressioni.
Le impronte in `sources.json` sono state ricontrollate contro i sorgenti finali.
Disassembly della build: nessun registro YMM/ZMM/TMM trovato; PE senza import DLL.
Il contratto rimane x87/SSE, non supporto AVX/AMX.

ASan riguarda i percorsi C host strumentati; Assembly e switch reale non hanno
fiber hooks ASan. Le fixture allocatore/CPU/PTE sono mock dove indicato.
Valgrind non eseguito; coverage linee/branch **N/D**. Accounting fisico e PTE
nel kernel non costituiscono una misura universale di assenza leak.

## Evidenze QEMU

Immagine: `kernel/build/scheduler-wait-native/kernel.elf`.
SHA256: `A67D128563CCF43A9E0E37D8266400CBA33CCAD92BC65382E3D79D5FA9519183`.
Primo boot esplorativo passato, seguito da **dieci boot consecutivi finali**:
`kernel/build/scheduler-firmware-eb4e109a3c4842fdbbe3afa8d785268e/`.

Ogni PASS richiede KERNEL_SCHEDULER_WAIT_OK e i marker switch/cleanup/ready,
210 dispatch, budget fisico ripristinato, CR3 atteso, RSP sullo stack principale,
marker CPU finale e tutte le 144 PTE task assenti. ESP/hash verificati per ogni
boot, OVMF/q35/TCG/max, 128 MiB, una CPU, rete assente, deadline 40 secondi;
lo script termina soltanto la VM posseduta.

La prova isolata senza produttore e passata:
`kernel/build/scheduler-firmware-332e8cf546054ef78ca4837b79a9a405/`.
Richiede due WAITING, nessun task terminato, 203 tentativi di dispatch,
owner/lease/pin validi per entrambi gli stack e budget iniziale meno 32 pagine.
Il marker BLOCKED_OK precede il fail-stop atteso; il verificatore controlla
CR3, marker CPU di arresto e 32 PTE dati vive con guardie assenti. Queste
risorse sono intenzionalmente conservate all'arresto, non riportate come leak
risolto o cleanup riuscito.

Questa variante ha inizialmente fallito la compilazione per codice irraggiungibile
dopo il percorso terminale. Le istruzioni di exit normale sono ora escluse
soltanto nel profilo di deadlock intenzionale. Dopo il fix sono stati ripetuti
backend, build normale e dieci boot finali sopra; nessun warning e ignorato.
Le regressioni guardie/NX/revoca/rollback sono registrate separatamente, non
conteggiate nei dieci boot positivi.

| Variante finale | Esito atteso verificato | Directory evidenze |
|---|---|---|
| guard-low | PF14, CR2 `0x700000140000`, error0 | `kernel/build/scheduler-firmware-ef4348e98db2451ebc0e80608dcc8bf6/` |
| guard-high | PF14, CR2 `0x700000151000`, error0 | `kernel/build/scheduler-firmware-c6c65d58f0064110be497ec898080386/` |
| nx | PF14, CR2 `0x700000141000`, error17 | `kernel/build/scheduler-firmware-fd36f7ca0e0643eca00ecd00ab814c61/` |
| retired | PF14 su mapping revocato, error0, tutte le 144 PTE zero | `kernel/build/scheduler-firmware-5402638e7a0642d7a498e4ab45e3172b/` |
| rollback | Rilascio verificato prima di abort, retry unico, WAIT_OK e KERNEL_READY | `kernel/build/scheduler-firmware-8973f383c96648d1a87e3d0549bfc44c/` |
| blocked | Arresto diagnostico atteso, due stack validi conservati | `kernel/build/scheduler-firmware-332e8cf546054ef78ca4837b79a9a405/` |

Tutte **6/6** passate dopo il fix finale; nessun crash inatteso o flaky osservato.
Ogni directory contiene immagini/hash, seriale, registri CPU e PTE lette dal
monitor. I fault intenzionali non sono successi del profilo positivo.
Gate stile/dimensione: 33 file sorgente/script del modulo, massimo350 righe;
massimo cinque file diretti per cartella, zero trailing whitespace/tab nei sorgenti.
Kernel main resta a 400 righe. Verificati 45 link locali e diff finale.

## Riproduzione

```powershell
& ./kernel/scheduler/scripts/test.ps1
& ./arch/x86/kernel/scheduler/scripts/test.ps1
& ./kernel/scripts/build.ps1 -OutputDirectory D:/OS/kernel/build/scheduler-wait-native
& ./arch/x86/kernel/scheduler/scripts/firmware.ps1 -Boots 10 -KernelDirectory D:/OS/kernel/build/scheduler-wait-native
```

Per il deadlock: build con `-TestSchedulerFault blocked` in directory separata,
poi firmware con `-Fault blocked -Boots 1` sulla stessa directory. Le varianti
di fault rifiutano Install. Nessuna installazione nell'ESP principale o push
e stato eseguito in questo blocco.

## Limiti e prossimo passo

API native e entry task rimangono private al carico diagnostico; non esiste
ancora un servizio pubblico generale di creazione task o di attesa per driver.
Niente preemption, IRQ produttori, timer sleep, timeout/cancellazione waiter,
SMP, ring3 o test fisici. Gli otto eventi durano quanto il run e non si distruggono.
Un segnale e una notifica: non trasferisce payload o permessi consumabili.

Prima di integrare device occorre progettare la pubblicazione degli eventi
da IRQ e il consumo dal dispatcher, con lifetime, ordering, overflow e idle
contro wake perso. Abilitare IF/HLT direttamente non e consentito dal profilo
provato. La versione attuale mantiene i controlli conservativi di mapping e
ownership a ogni transizione; overhead e latenza non sono stati misurati.
