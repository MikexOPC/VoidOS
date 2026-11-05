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

## K4-C2 — Timer periodico sugli AP

Il profilo isolato TestApService ap-timer usa il reload ottenuto dal
timer one-shot del BSP con divisore 16. Gli AP usano il vettore 0xd1.
Il BSP attende ACK e ritorno IRETQ per arm, PING e stop. Fra arm e stop
esegue anche uno shootdown sul gruppo. L'handler valida il bit ISR
e pubblica il conteggio dopo EOI.

- 10/10 boot QEMU xAPIC a quattro CPU. Tutti i 30 AP osservati
  hanno riportato dieci consegne. Dopo lo stop, 40 ms di drenaggio
  seguiti da 100 ms di osservazione con contatore invariato.
  Evidenza: kernel/build/ipi-service-ec99d86c7d954211a0e8deb960f37c72.
- Suite ipi-service e layout passate.
  Evidenze: kernel/build/test-run-aa452f867292444091ceabdddafcc8c0 e
  kernel/build/test-run-de2897c0d3b14ed39cd00de4497f275b.
- 10/10 boot ordinari a quattro CPU.
  Evidenza: kernel/build/ap-runtime-95ad7ee35c7243a2a9e48364af26ad90.
- Audit di emergenza passato sulle immagini diagnostica e ordinaria.

La prova misura le consegne aggregate su QEMU. Jitter e comportamento
su hardware fisico restano da misurare. Il profilo non effettua switch
preemptivi. Coverage non misurata.

## K4-C3 — Controllo dell'area salvata

Il dispatcher IRQ verifica in Rust l'estensione del frame, dei 15
registri e della zona FXSAVE sullo stack autorizzato prima di leggere
RIP e RSP. I test coprono guardia, disallineamento, fine stack e
intervalli non canonici.

- Suite kernel, x86-scheduler e layout passate. Evidenze in
  kernel/build/test-run-c76ae287e2f6453dab0a9577a3de9d5a,
  kernel/build/test-run-86c6d63b7ddb419d9707ebe7efaf81df e
  kernel/build/test-run-2a5a8de940fa4e5ca83bc06c8c1e9674.
- QEMU a 4 CPU: prima campagna 2/10, con arresto generico dello
  scheduler nel boot 3. Il trace non mostra il vettore 0x41 prima
  dello stop. Evidenza: kernel/build/ap-runtime-f1fbfb0b3d7f4fda9da7c96f2f1cde96.
- Seconda campagna, a suite host terminate: 10/10.
  Evidenza: kernel/build/ap-runtime-80be882ff28348a1a693872768f80ff2.

La causa dell'arresto iniziale resta aperta. Nessuno switch preemptivo
è ancora collegato. Coverage non misurata.

### Diagnostica dell'IRQ sul BSP

Il diagnostico distingue armamento fallito, attesa scaduta senza
consegna e completamento incoerente. Il test irq-timeout ferma il
timer dopo l'armamento e richiede che il poll scada con registri
e FXSAVE intatti. Lo stop atteso conserva le risorse del test.

- 10/10 boot ordinari QEMU a quattro CPU sull'immagine finale.
  Evidenza: kernel/build/ap-runtime-6a2c0402beae47eb877ba0696b5bd1ac.
- 1/1 arresto atteso irq-timeout, con prova CPU e memoria.
  Evidenza: kernel/build/scheduler-firmware-5172deff3c8846d7919e8c4646195b81.
- Suite x86-scheduler, kernel e layout passate. Evidenze:
  kernel/build/test-run-96ddc10faf1e4171a65cea5af1955067,
  kernel/build/test-run-bed4189df7c74c65b337a601bc53cca9 e
  kernel/build/test-run-8b2c2ada1f49467388ef41a8a7c96755.

L'arresto spontaneo iniziale non si è ripetuto. Il trace non contiene
una consegna 0x41, ma il vecchio codice terminale non distingue
l'armamento dall'attesa. La causa originaria non è dimostrata.

### Prima preemption del BSP

Il profilo TestSchedulerPreemption usa il timer one-shot esistente.
Dopo l'EOI, l'epilogo IRQ chiama il percorso di sospensione con
il frame ancora sullo stack del task. Il dispatcher esegue tick_on
e PREEMPT tramite il gateway Rust per CPU. Un secondo task già
pronto prosegue prima che il primo riprenda tramite IRETQ.

- 10/10 boot QEMU a quattro CPU, con marker di preemption obbligatorio.
  Evidenza: kernel/build/ap-runtime-05f9aac71ab04ad7afcb2020d8c9d988.
- Il workload controlla sette GPR e l'immagine FXSAVE. Il marker
  arriva dopo l'esecuzione del secondo task, la ripresa e il cleanup.
- Suite x86-scheduler, scheduler e layout passate. Evidenze:
  kernel/build/test-run-72e3079edcf849b49d3920348d93b48d,
  kernel/build/test-run-14998121be2f4fc8962dce896f6728bf e
  kernel/build/test-run-4602125d84fe46a7a581bc0e119dc629.
- 10/10 boot ordinari a quattro CPU e audit di emergenza su entrambe le immagini.
  Evidenza: kernel/build/ap-runtime-73f296bae8dc4e0db20fa61f302b5a12.

I primi tentativi si sono fermati perché l'argomento MASM per
attivare il ramo era dopo il sorgente. Nel disassemblato finale la
chiamata è presente tra il dispatch IRQ e FXRSTOR64.
La prova è un singolo switch preemptivo sul BSP. L'uso periodico
nello scheduler e il dispatcher AP richiedono i blocchi successivi.

### Preemption periodica sul BSP

Il profilo TestSchedulerPeriodic chiude la sessione one-shot e riusa
il reload calibrato. Due task senza yield alternano sei preemption,
tre ciascuno, con otto ingressi del dispatcher inclusi i ritorni finali.
Ciascun workload controlla sette GPR e l'immagine FXSAVE dopo la
ripresa. Il sesto IRQ arresta il timer. Lo stop richiede ISR e IRR
vuoti, distacco del callback, LVT e divisore ripristinati e budget
fisico invariato dopo la raccolta degli stack.

- 10/10 boot diagnostici QEMU a quattro CPU.
  Evidenza: kernel/build/ap-runtime-c447cbdd8ad147df987184ad7018b636.
- Suite x86-scheduler, kernel e layout passate. Evidenze:
  kernel/build/test-run-07add93eaad9493e8a8304e4c475ab52,
  kernel/build/test-run-82d4354b12104bffb1cddd0d70c1c32c e
  kernel/build/test-run-a446762f4cfb43078cd369857ffdcbc1.
- 10/10 boot ordinari QEMU a quattro CPU.
  Evidenza: kernel/build/ap-runtime-b5c96fed0f8744fca30b838dd4eec005.
- Audit di emergenza superato sulle due immagini.

Il primo tentativo si è fermato prima dell'armamento: lo slot di
profilo 5 attivava il controllo budget PS/2 anche senza quel driver.
Il controllo ora è condizionato al profilo PS/2. Il diagnostico usa
il solo BSP. Jitter, preemption su AP e stress restano da provare.

## K4-C4 — Gate dell'identità CPU

5 novembre 2026. Primo sottoblocco di C4. Nel profilo kernel con
scheduler_core_lock il gateway verifica il contesto locale e il parametro
CPU prima di accedere allo stato Rust. Identità diversa, contesto assente
o risposta diversa da uno restituiscono DENIED. La verifica avviene sotto
il lock, che viene rilasciato anche dopo un rifiuto. Il gate BSP esistente
rimane un requisito del backend nativo.

- Suite scheduler e x86-scheduler passate sull'ultima revisione. MSVC e
  Rust con warning come errori, ASan sui fixture C previsti dalle suite,
  Clippy anche con scheduler_core_lock e cppcheck senza diagnostiche
  error/warning/performance/portability. Evidenza congiunta in
  kernel/build/c4-authority-host-4db516b353a041d4a7eeb89ef877a79c.
- Test Rust di stato, binding e payload in debug e release, 42 casi per
  configurazione. Le regressioni FFI e dispatcher usano il vero oggetto
  Rust e backend host modellati.
- 136 controlli sul vero gate C con accessor CPU e predicate dispatcher
  mock. Limiti CPU, identità diversa, stato non attivo, accessor assente
  e risultato non normalizzato. Il fixture non alloca memoria dinamica.
- Stress FFI con lock, cinque esecuzioni da 20.000 task su quattro thread.
  Identità per thread, eventi e stop sulle quattro identità, rifiuto delle
  operazioni BSP da thread AP e contatori di acquire/release bilanciati.
  Le generazioni delle prime prenotazioni verificano che i rifiuti non
  abbiano consumato slot. Evidenze in
  kernel/build/scheduler-host-f24376a62b6d4964ae2e4b2cfb2d27a9.
- Backend C e Assembly, preservazione dei registri, stack e regressioni
  IRQ passati nella suite x86-scheduler. Evidenze in
  kernel/build/scheduler-context-d2e688e03dc748c39e84afd7f74be453.
- Controllo layout centralizzato passato.

Campagne QEMU/OVMF sulla revisione finale, TCG, quattro vCPU. Livello
complesso, dieci boot consecutivi per profilo. Entrambe le immagini sono
ricompilate e confrontate con la copia ESP. Hash di kernel e loader e
verifiche di CPU, memoria e cleanup nei results.json dei runner.

- Ordinario, 10/10. Evidenza in
  kernel/build/ap-runtime-3a8b897886ef4382a6e938d46d7707c1.
- Preemption periodica BSP, 10/10. Marker di sospensione, ripresa e stop
  periodico verificati in ogni boot. Evidenza in
  kernel/build/ap-runtime-7de6b469d1734edcb41d2cce864357cd.
- Campagna con immagini e log di build in
  kernel/build/qemu-run-95dfc8d5101b4b44927fd72769f5f0b5.

Diff finale verificato. I sorgenti modificati rispettano il limite di
1.000 righe e le cartelle funzionali restano entro otto file.

Limiti della prova. I thread host modellano le CPU. Gli AP dei profili
QEMU rimangono nel servizio PING/flush. Il dispatcher e lo switch dei task
sugli AP sono i prossimi collegamenti di C4. Coverage non misurata.
Le prove ASan host e i contatori dei lock hanno un ambito distinto
dall'accounting della memoria fisica verificato dal profilo firmware.

## K4-C4 — Motore indirizzabile per CPU

5 novembre 2026. Secondo sottoblocco. dispatch_on inoltra le operazioni
alla CPU verificata e usa una guardia d'ingresso per identità. Il percorso
legacy rimane BSP. La chiusura del lotto resta esclusiva: non è ancora
il protocollo per dispatcher AP concorrenti.

- Suite scheduler passata: test Rust in debug/release, regressioni C/FFI,
  dieci modalità del motore e cinque stress da 20.000 task su quattro
  thread. Evidenze in
  kernel/build/scheduler-host-0e66afd1ed8c4cfbadbf07c4a0550ecb.
- Nuovo fixture: 16.581 controlli su 64 identità CPU sequenziali,
  vero Owner Rust con scheduler_core_lock. Affinità, yield, eventi,
  wait con epoca cambiata, stop/query, exit e raccolta. Rifiutati CPU
  falsa, indici fuori limite, gate zero/non normalizzato, rientro e
  drain IRQ AP. Il gate viene ricontrollato dopo il callback run;
  una nuova sessione verifica il rilascio della guardia dopo errore.
  Acquisizioni/rilasci del lock bilanciati; callback fuori dal lock.
- Suite x86-scheduler passata, inclusi preservazione dei registri,
  stack, IRQ e 136 controlli del gate nativo. Evidenze in
  kernel/build/scheduler-context-86bb8e0f61cf45dcb61d7320cd13f483.
- MSVC /W4 /WX e Rust -D warnings, ASan sui fixture C, Clippy e
  cppcheck senza error/warning/performance/portability. Le segnalazioni
  informative cppcheck sono conservate nei report XML.
- Layout centralizzato e git diff --check passati. Sorgenti modificati
  fino a 180 righe; cartelle funzionali entro otto file.

Campagna QEMU/OVMF TCG a quattro vCPU, livello complesso: immagini
ricompilate, hash confrontati con ESP e dieci boot consecutivi per
profilo. Verifiche di CPU, memoria e cleanup nei results.json.

- Ordinario 10/10:
  kernel/build/ap-runtime-4de40aa6350a430294e7a3509c53ad2b.
- Preemption periodica BSP 10/10:
  kernel/build/ap-runtime-fa3d5acae1134ffa84d487e9782ec920.
- Build, immagini e riepilogo:
  kernel/build/qemu-run-5627ac9414e244cea257e6be1693a722.

Limiti: nel nuovo fixture CPU, stack e risorse native sono mock; nessuna
allocazione dinamica è introdotta. ASan non misura leak della RAM fisica.
Il kernel mantiene un solo dispatcher BSP e tre AP nel servizio esistente.
Nessuna prova di switch task sugli AP o di lotti concorrenti è dichiarata.
Coverage linee/branch non misurata. Nessun nuovo accesso hardware.
