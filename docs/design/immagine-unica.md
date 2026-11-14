# Design: immagine unica

Stato: scelta l'alternativa C (prima A, poi B), 13 novembre 2026. Parte A
fatta, parte B da progettare. Da accorpare in `docs/design/consolidamento.md`
dopo il riordino della documentazione.

## Risultato della parte A

L'immagine Meson `ordinary` contiene shell, tastiera e mouse PS/2, console,
job su AP1, processo utente e servizio residente `bridge`. È la stessa
immagine delle campagne `k6-shell` e `k6-shell-bridge-disabled`.

- Piano di build: 310 comandi, identici a `build.ps1` con
  `-Ps2Input -Ps2Mouse -ConsoleShell -InteractiveInput`.
- Il runner Rust considera riuscito l'avvio quando la shell è pronta
  (`KERNEL_PS2_INPUT_READY`); i runner Python che controllano il BSP fermo
  scrivono prima `exit`.
- Senza controller PS/2 (`k6-no-ps2`, QEMU `i8042=off`) l'immagine segnala
  `KERNEL_PS2_INPUT_ABSENT`, esegue il processo e arriva a `KERNEL_READY`.
- Restano: runtime dentro l'avvio del timer, arresto dopo `exit` o senza
  PS/2, GPU VirtIO fuori dall'immagine. Sono la parte B.

## Obiettivo

Una sola immagine di produzione che si avvia, usa tutte le CPU, esegue il
programma utente, offre la shell e non si ferma: resta in un ciclo di attesa
con gli interrupt attivi. Le funzioni oggi "solo diagnostiche" che servono
all'uso entrano nell'immagine; le altre restano prove separate.

## Come funziona oggi

Il profilo `ordinary` (Meson) definisce `KERNEL_SCHEDULER_IRQ`,
`KERNEL_AP_RUNTIME`, `KERNEL_USER_PROCESS` e `KERNEL_PANIC_RUNTIME`.

Tutto il lavoro dopo l'inizializzazione avviene **dentro l'avvio del timer
LAPIC** (`arch/x86/kernel/timer/lapic/driver.c`), in questo ordine:

1. calibrazione del timer e tre scadenze di prova;
2. esecuzione del programma utente incorporato (`kernel_process_boot`);
3. solo con `KERNEL_PS2_INPUT`: sessione PS/2 con la shell, finché l'utente
   scrive `exit`;
4. chiusura di timer e watchdog, ritorno a `kernel_main`, `KERNEL_READY`,
   arresto della CPU con interrupt spenti.

| Funzione | Flag | Nell'immagine ordinaria |
|---|---|---|
| AP avviati e parcheggiati, PING e shootdown | `KERNEL_AP_RUNTIME` | Sì |
| Processo ring 3 incorporato | `KERNEL_USER_PROCESS` | Sì |
| Tastiera PS/2 e shell | `KERNEL_PS2_INPUT`, `KERNEL_CONSOLE_SHELL` | No |
| Mouse e input interattivo | `KERNEL_PS2_MOUSE`, `KERNEL_INPUT_INTERACTIVE` | No |
| Job su AP1 dalla shell (`ap <n>`) | `KERNEL_AP_TASKS` | No, attivo solo con shell |
| Servizio residente ring 3 (`bridge status`) | shell + processo | No |
| GPU VirtIO tradotta da VT-d | `KERNEL_VGPU_IOMMU` | No |
| Preemption da timer | solo flag `KERNEL_TEST_*` | No |

Tre problemi strutturali:

- **Il sistema vive dentro l'inizializzazione di un driver.** Shell e
  processo girano nel mezzo della funzione che avvia il timer.
- **L'immagine termina.** Dopo `KERNEL_READY` la CPU si ferma: contro la
  regola 2.1 delle direttive.
- **Le funzioni si attivano a compilazione**, quindi ogni combinazione è
  un'immagine diversa.

## Alternative

### A. Promuovere il profilo shell

L'immagine ordinaria prende i flag del profilo `k6-shell`: PS/2, mouse,
console, shell, job su AP1, servizio residente. Nessun cambio di struttura.

- Pro: piccolo, già provato (`k6-shell` passa a ogni giro). Una sola immagine
  con shell, SMP e processi utente.
- Contro: restano il runtime dentro il timer e l'arresto dopo `exit`. Senza
  tastiera la shell aspetta e basta.

### B. Ciclo del kernel fuori dal driver

`kernel_main` diventa: inizializzazione, avvio dei servizi (timer, input,
console, processo), poi un ciclo di attesa con gli interrupt attivi che non
termina. La shell diventa un task dello scheduler. Il timer LAPIC torna a
essere solo un driver.

- Pro: struttura giusta, ed è la base dello scheduler preemptive e dei
  processi multipli.
- Contro: lavoro grande. Tocca boot, timer, watchdog, sessione PS/2 e
  scheduler insieme; ogni pezzo richiede 10 boot e stress.

### C. Prima A, poi B (consigliata)

Due obiettivi in fila. A dà subito un'immagine unica provata dalla CI. B la
ristruttura, con l'immagine unica come banco di prova invece di un profilo
diagnostico.

## Decisioni da prendere

1. **Alternativa**: A, B o C.
2. **GPU VirtIO con VT-d**: dentro l'immagine (rilevata all'avvio, GOP se
   assente) o fuori finché non è rilevabile a run-time. Proposta: fuori nel
   primo passo, perché oggi è un'opzione di compilazione.
3. **Comando `exit` della shell**: con A resta e ferma il sistema; con B
   diventa riavvio oppure sparisce. Proposta: con B, riavvio.
4. **Senza tastiera** (CI, macchina senza PS/2): l'immagine deve comunque
   arrivare a "shell pronta" e aspettare. La CI verifica quel marker invece
   di `KERNEL_READY`.

## Criteri di chiusura

Per A:

- Una sola immagine Meson `ordinary` con shell, mouse, job AP1, processo
  utente e servizio residente.
- La CI avvia quell'immagine e verifica shell pronta, processo eseguito,
  quattro CPU.
- Campagne `k6-shell`, `k6-ordinary`, `k6-protection` su quell'immagine,
  10 boot; campagne con shell ripetute sulla stessa immagine.
- I profili `k6-shell` e `k6-ordinary` diventano la stessa immagine; la
  differenza resta solo nel runner.

Per B, in più:

- Nessuna logica di runtime dentro `timer/lapic/driver.c`.
- Dopo l'avvio il BSP resta in un ciclo con interrupt attivi; nessun
  arresto.
- 10 boot e una sessione di almeno 30 minuti senza errori né perdite di
  memoria.

## Rischi

- Con A l'immagine ordinaria inizializza PS/2 e console anche dove non
  servono: va verificato il comportamento senza controller PS/2 (oggi
  `KERNEL_PS2_INPUT_ABSENT` esiste, non è provato nell'ordinaria).
- Il boot in CI diventa più lungo: una shell che aspetta deve essere
  riconosciuta come successo, non come VM silenziosa.
- B cambia l'ordine di avvio di più moduli: è il punto con più rischio di
  regressioni su SMP e watchdog.

## Parte B: proposta (14 novembre 2026)

### Cosa c'è davvero oggi

Ricerca nel codice, nessuna prova nuova eseguita.

- **Lo scheduler lavora a sessioni in sequenza.** `next_session_private`
  in `arch/x86/kernel/scheduler/integration/kernel.c` ammette le sessioni
  0-4 una dopo l'altra; ognuna deve finire prima della successiva.
- **Le sessioni 0-3 sono carichi di prova.** IRQ (`irq/bridge.c`), idle e
  "persistente" (`idle/idle.c`, `persistent/workload.c`, 32 giri
  produttore/consumatore) girano durante l'avvio del timer, a ogni boot
  dell'immagine ordinaria. Anche le tre scadenze del timer sono una prova.
- **La sessione 4 è la shell** (`x86_scheduler_run_input`): task di
  tastiera, mouse, shell e console, finché l'utente scrive `exit`.
- **Il watchdog HPET non è un limite.** Si arma a ogni attesa con `HLT`
  (100 ms) e protegge il ciclo continuo del BSP; il "budget" è contabilità
  di pagine prestate, non una scadenza.
- Tutto questo è chiamato dall'interno dell'avvio del timer LAPIC
  (`timer/lapic/driver.c`).
- **Le prove fanno anche l'inizializzazione.** `deadlines()` in
  `timer/lapic/driver.c` registra l'interrupt del timer (vettore 0x41) e
  ogni scadenza avvia una sessione di prova (0: IRQ, 1-2: idle e
  persistente, 3: idle ripetuto). La sessione della shell parte solo se
  `irq_profile` vale 4, cioè dopo le sessioni 0-3. Toglierle richiede di
  separare prima l'inizializzazione dal collaudo.

### Alternative

**B1. Riordino con il modello a sessioni (consigliata ora).**

1. Il driver del timer fa solo il timer: calibrazione e avvio. La sequenza
   (processo utente, sessione della shell) la guida `kernel/startup`.
2. Le sessioni di prova 0-3 e le scadenze di prova escono dal percorso di
   produzione: restano nei profili di test e nei test host.
3. La sessione della shell resta l'ultima. Alla fine il sistema non si
   ferma da solo: senza tastiera, o dopo che la sessione finisce per un
   motivo diverso da `exit`, il BSP entra in un ciclo di attesa con gli
   interrupt attivi.
4. `exit` resta un arresto chiesto dall'utente.

- Pro: struttura giusta, rischio contenuto, il modello di scheduler
  provato resta.
- Contro: lo scheduler resta a sessioni con 8 task fissi; più processi e
  preemption richiedono comunque il punto successivo della roadmap.

**B2. Scheduler persistente unico.**

Un solo ciclo dello scheduler che non termina mai: input, shell, bridge e
servizi AP sono task permanenti, un task idle fa `HLT` con interrupt
attivi.

- Pro: è il modello finale.
- Contro: coincide con l'obiettivo "scheduler preemptive su tutte le CPU"
  del punto 1. Farlo qui mescola due obiettivi, contro la regola 2.2.

### Decisioni da prendere

1. **Alternativa**: B1 ora e B2 dentro l'obiettivo dello scheduler, oppure
   B2 subito. Proposta: B1.
2. **Prove di avvio dello scheduler** (sessioni 0-3): fuori dal percorso di
   produzione. Effetto: l'avvio perde un controllo automatico del percorso
   IRQ, che resta coperto dalle campagne dei profili di test. Proposta:
   toglierle, come vuole la regola 2.1.
3. **`exit`**: arresto della CPU con messaggio (come oggi), oppure riavvio
   tramite il registro di reset della tabella ACPI FADT. Il riavvio cambia
   i runner Python che controllano il BSP fermo. Proposta: arresto ora,
   riavvio in un passo successivo.
4. **Senza tastiera**: ciclo di attesa con interrupt attivi e un marker
   `KERNEL_IDLE`; la campagna `k6-no-ps2` verifica quel marker.

### Criteri di chiusura di B1

- `timer/lapic/driver.c` non chiama più processo, sessione PS/2 o carichi
  dello scheduler.
- Nessun carico di prova nell'immagine ordinaria (verifica sul piano di
  build e sulla mappa del linker).
- Senza tastiera il BSP resta in attesa con IF=1 per tutta la prova, senza
  arresto.
- Le 15 campagne dell'immagine ordinaria, a boot pieni, e la CI verdi.
- Una sessione di almeno 30 minuti con la shell aperta senza errori né
  perdite di memoria.

### Task 2: inizializzazione o collaudo (ricerca, 14 novembre 2026)

| Sessione | Avvio | Contenuto | Inizializza |
|---|---|---|---|
| 0 base | `kernel/startup/platform.c` | 2 task, 210 passi di prova | Sì: `KERNEL_SCHEDULER_INIT`, pool e radice salvati per le sessioni dopo |
| 1 IRQ | scadenza 1 del timer | percorso IRQ dei task | No |
| 2 idle | scadenza 2 | attesa con `HLT` | No |
| 3 persistente | scadenza 2 | 32 giri produttore/consumatore | No |
| 4 idle ripetuto | scadenza 3 | ripete l'attesa | No |
| 5 shell | sessione PS/2 | tastiera, mouse, shell, console | Runtime |

Le tre scadenze di `deadlines()` registrano l'IRQ del timer (0x41), che
viene comunque scollegato prima della shell: durante la shell le attese
usano il watchdog HPET. Il resto serve solo a portare `irq_profile` a 5.

Conseguenze per la task 3: separare `KERNEL_SCHEDULER_INIT` e il
salvataggio di pool e radice dal carico della sessione 0; permettere il
passaggio diretto alla sessione 5; togliere dal percorso ordinario le
scadenze e i controlli che contano tre consegne (`timer.Count == 3`).

## Risultato di B1 (14 novembre 2026)

- **Timer**: `x86_timer_run` fa solo il timer; processo e shell sono chiamati
  dall'integrazione del watchdog fra `x86_timer_run` e `x86_timer_finish`.
- **Nessun collaudo nell'immagine con la shell**: `kernel_scheduler_prepare`
  inizializza lo scheduler senza la sessione 0; niente scadenze di prova del
  timer né sessioni 1-4. Il codice di collaudo non è più collegato (mappa
  del linker verificata; 496 -> 484 KB). Le immagini senza sessione
  interattiva, cioè i profili diagnostici, eseguono ancora tutta la sequenza.
- **Nessun arresto automatico**: dopo `KERNEL_READY` l'immagine stampa
  `KERNEL_IDLE` e resta in `STI; HLT`, salvo `exit` dell'utente.
- **Sessione lunga**: scenario `--ordinary-soak` del runner; esito sotto.

Limiti rimasti: il ciclo di attesa finale non ha servizi attivi; lo
scheduler resta a sessioni con 8 task fissi; il runtime parte ancora
dall'integrazione del watchdog in `arch/x86`, non da `kernel/startup`;
la memoria controllata è il budget di pagine fisiche, non l'heap.

Sessione lunga: `--ordinary-soak 30` passata, 30 minuti, 311 giri di
`bridge status` riusciti, 312 comandi contati dal kernel, chiusura pulita
di scheduler, mouse, input e watchdog (contatori di errore del watchdog a
zero), budget di pagine fisiche invariato alla chiusura.
