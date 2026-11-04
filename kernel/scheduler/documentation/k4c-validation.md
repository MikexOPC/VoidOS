# K4-C — Validazione

## K4-C1: core Rust SMP

### Cosa cambia

Un solo `Owner` Rust serve tutte le CPU del gruppo.

- **`current` e `remaining` per CPU.** Ogni CPU ha al più un task
  RUNNING e i tick rimasti del suo timeslice. Un task RUNNING compare in
  un solo `current`, e la sua maschera contiene quella CPU.
- **Affinità.** Ogni task riservato ha una maschera di CPU, per default
  tutto il gruppo. Si cambia solo da RESERVED. Gli slot Free e Retired
  hanno maschera zero.
- **Configurazione.** `configure(cpus, slice)` fissa la dimensione del
  gruppo (1–64) e il timeslice in tick (1–1.000.000) quando non esiste
  alcun task. Il default è una CPU con slice 1, quindi il comportamento
  del boot attuale non cambia.
- **Selezione.** `select_on(cpu)` prende il task READY più vecchio la cui
  maschera contiene `cpu`, lasciando invariato l'ordine degli altri.
  BLOCKED significa che il lavoro esiste altrove (task in attesa, legati
  ad altre CPU o in esecuzione altrove); EMPTY significa che non resta
  nulla. Il dispatcher continua con l'idle nel primo caso e termina nel
  secondo.
- **Operazioni del dispatcher.** Reserve, publish, abort, collect, stop e
  drain degli IRQ richiedono che la CPU chiamante non abbia un task
  RUNNING, come prima per l'unica CPU.
- **Tick e preemption.** `tick_on(cpu)` consuma un tick del task corrente
  e restituisce "da interrompere" quando lo slice è finito e un altro
  task READY può girare su quella CPU. Un task solo resta in esecuzione
  con lo slice esaurito. `preempt_on` è la stessa transizione di yield:
  il task torna in coda.
- **API della CPU 0.** Le funzioni senza `_on` restano per i test e
  delegano alla CPU 0. Il gateway usa solo le varianti per CPU.

### Lock del core

Il gateway entra nell'`Owner` tramite un guard RAII (`ffi/core_lock.rs`):
prende il lock, poi segna l'ingresso, e il `Drop` rilascia entrambi su
ogni percorso di uscita.

- Con `--cfg scheduler_core_lock` (build del kernel e stress SMP) il
  lock è un `KERNEL_LOCK` di `kernel/sync`, preso dal backend C
  `arch/x86/kernel/scheduler/integration/lock.c` con un guard per CPU.
- Rank 900, foglia: dentro il lock l'`Owner` non chiama callback e non
  prende altri lock. Ogni detentore di lock gira già con IF=0, quindi un
  tick non può annidarsi dentro un detentore sulla stessa CPU.
- Il lock è inizializzato dal BSP al primo uso, prima che un AP possa
  entrare nello scheduler.
- Senza il cfg resta il solo flag di ingresso, come prima. Così i test
  host di altri moduli che linkano il gateway non cambiano.

### Prove

- Suite `scheduler`: test Rust 34 + 3 + 5 in debug e release, Clippy e
  cppcheck senza diagnostiche bloccanti. Nuovi test SMP: limiti di
  configurazione, nessun task su due CPU, affinità, tick e preemption,
  wait per CPU, passeggiata casuale di 200.000 operazioni su 4 CPU.
- Stress C sul vero oggetto Rust con il lock del core: 4 thread come
  4 CPU, 20.000 task, circa 13.300 preemption per esecuzione, 5/5
  esecuzioni senza task su due CPU e coda vuota alla fine.
  Evidenza: kernel/build/scheduler-host-5e4f7efac60d4c9bb57e128cb355ff4e.
- Regressioni host dei moduli che linkano il gateway: x86-scheduler,
  console, pointer, x86-virtio, x86-framebuffer, paging,
  virtio-discovery, virtio-mapping, layout.
- QEMU, kernel con il lock vero (livello medio, 5 boot):
  boot ordinario a 4 CPU 5/5
  (kernel/build/ap-runtime-a27523eb89b74d92b70b9b52a958bc05),
  una CPU 5/5 (kernel/build/ap-runtime-b638ce3a3387461186631d0e78b03f39).

Limiti: lo scheduler del kernel usa ancora solo la CPU 0. Timer per CPU,
entry di preemption e dispatcher sugli AP sono C2–C4.
