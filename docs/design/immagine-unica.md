# Design: immagine unica

Stato: proposta, 13 novembre 2026. Da accorpare in `docs/design/consolidamento.md`
dopo il riordino della documentazione. Nessun codice scritto.

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
