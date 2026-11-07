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

## K4-C4 — Contesti nativi separati

5 novembre 2026. Terzo sottoblocco. Contesto del dispatcher, fase,
task/generazione, richiesta e statistiche sono ora per CPU. Slot di
704 byte allineati a 64, 45.056 byte statici complessivi. Owner e self
devono corrispondere all'identità verificata. Preparazione ripetuta senza
reset dei dati; nessun nuovo allocatore o lease. Dopo gli switch espliciti
il codice dei task riacquisisce il contesto locale.

- Nuovo test C con ASan: 2.486 controlli, 64 slot distinti, limiti e
  identità incoerenti rifiutati, rebind vietato, contesti preservati.
  Quattro thread aggiornano i propri slot per 100.000 iterazioni ciascuno.
  Accessor CPU simulato; nessuna misura di esecuzione AP in questo test.
- Suite x86-scheduler passata, con prove Assembly di preservazione,
  stack, IRQ, gate CPU e watchdog. MSVC /W4 /WX e cppcheck senza
  diagnostiche error/warning/performance/portability. Evidenze in
  kernel/build/scheduler-context-eb2df731dc4b44b9a0ef6281dd65fe2c.
- Suite scheduler passata: Rust debug/release, Clippy, FFI con ASan,
  stress a quattro thread, dispatcher e identità CPU. Evidenze in
  kernel/build/scheduler-host-cb65441ff0534d18a8f9fe72f3f6557f.
- Layout centralizzato e diff verificati. integration contiene otto
  sorgenti/header; kernel.c è di 476 righe. Formato Assembly invariato.

QEMU/OVMF TCG a quattro vCPU: immagini ricompilate e hash ESP verificati,
livello complesso con dieci boot consecutivi per profilo, nessun panic
inatteso. Accounting e risorse verificati dai runner.

- Ordinario 10/10:
  kernel/build/ap-runtime-db76108ff64647d6865182fec7d17e07.
- Periodico BSP 10/10, con marker preemption e stop periodico:
  kernel/build/ap-runtime-b24f02a81b3d4c2fb8d554eecd8ed35d.
- Build, immagini e riepilogo:
  kernel/build/qemu-run-2ad0584d9e0b47b0acf99088e10e1a16.

Limiti: scheduler ancora sul solo BSP; tre AP nel servizio esistente.
Gli stack dispatcher AP e il loro ingresso non sono ancora ammessi.
Migrazione preemptiva, orchestrazione concorrente e driver SMP restano
da collegare. ASan host non certifica leak della RAM fisica; nessuna
coverage linee/branch misurata. Rust conserva le decisioni sui task;
C possiede gli oggetti ABI e Assembly effettua gli switch già verificati.

## K4-C4 — Frame di servizio AP verificato in Rust

5 novembre 2026. Quarto sottoblocco. Prima di leggere il frame, C riusa
il validatore Rust dell'intera area salvata: frame, GPR, allineamento,
shadow space e FXSAVE. La geometria del ritorno idle, i selettori e i
flags passano a un modulo Rust con forbid(unsafe_code). Il wrapper FFI
riceve soli interi. Nessun nuovo accesso MMIO o cambiamento Assembly.
Verificati Intel SDM 078US §6.14.2/3 e il formato dell'entry esistente.

- Suite del servizio passata sulla revisione finale. Dieci test Rust
  in debug e dieci in release: tre nuovi casi coprono geometria, profili,
  selettori e 131.072 combinazioni flags/profilo per configurazione.
  Clippy con warning come errori passato.
- Vero collegamento C/Rust con ASan: casi validi e invalidi, frame/spill
  fuori stack respinti prima della dereferenziazione. Il test verifica
  anche il motivo preciso dell'arresto; MMIO resta mock e non viene usato
  dai rifiuti. Memoria del nuovo validatore senza allocazioni dinamiche.
- Regressioni mailbox: venti esecuzioni da 15.000 richieste concorrenti,
  nove casi di attesa e dieci test del runner firmware passati.
- MSVC /W4 /WX, cppcheck senza error/warning/performance/portability,
  layout centralizzato e diff passati. Sorgenti modificati fino a 261
  righe; cartelle funzionali entro otto file. Evidenze host:
  kernel/build/c4-ap-frame-host-4bd8396297434daba8b6d2d69f597667.

Campagna QEMU/OVMF TCG a quattro vCPU, livello complesso, dieci boot
consecutivi per profilo e immagini ricompilate con hash ESP verificati:

- Ordinario 10/10:
  kernel/build/ap-runtime-f3d765934cbf40f8b3912eb225c31cf6.
- PING/flush e preservazione GPR/FPU/SIMD AP 10/10:
  kernel/build/ipi-service-d495d1c672fd4c578967a2e79d4147be.
- Timer periodico sui tre AP, PING/flush e stop verificato 10/10:
  kernel/build/ipi-service-bba2c695889c42b3a304ba1e9375e4d0.
- Build, immagini e riepilogo:
  kernel/build/qemu-run-1d5020a2d2614bf79d83ab45a4c0a2ad.

Limiti: si tratta ancora del frame idle, non di autorizzazione per task
AP. La verifica C/Rust avviene dopo i salvataggi Assembly: non sostituisce
guardie, mapping e ownership dello stack. ASan host e accounting fisico
dei runner sono prove distinte; coverage linee/branch non misurata.

## K4-C4 — Prima ammissione AP fuori IRQ

5 novembre 2026. Scelta utente: stato Rust separato e PING esistente.
Implementato il diagnostico TestApService admission, campagna
ap-dispatch-admission. AP1 consuma una richiesta numerata, verifica il
contesto fuori IRQ, prepara il proprio slot nativo e pubblica ADMITTED.
Ritorna all'idle con registri, FPU e SIMD verificati. ACK PING e conferma
di ammissione sono distinti. La mailbox PING/flush/timer non cambia.

- Quindici test Rust in debug e quindici in release, warning come errori
  e Clippy passati. Cinque nuovi test: limiti/autorità, 63 destinatari,
  claim concorrenti, 512 corse conferma/fallimento e timeout deterministici
  prima/dopo il claim. FAILED non si riapre, token e gruppo errati respinti.
- FFI C/Rust con ASan: tre consumer concorrenti, 300.000 claim ripetuti
  respinti dopo l'ammissione, handle host chiusi. Test nativo con stack
  simulato invalido: nessuna pubblicazione del record e FAILED terminale.
- Regressioni mailbox, nove casi delle attese e dieci casi del runner
  passati. MSVC /W4 /WX e cppcheck passati, senza diagnostiche bloccanti.
  Evidenze host: kernel/build/c4-admission-host-836f94b151d249fc9b76d6e43905c0bc.
- Layout e diff verificati. Il nuovo stato non usa heap; le risorse AP
  esistenti restano pinned. ASan host non misura leak della RAM fisica.

Campagna finale QEMU/OVMF TCG, quattro CPU reali inizializzate, dieci boot
consecutivi per profilo complesso. Immagini ricompilate, hash kernel/loader
ed ESP verificati, accounting e snapshot CPU/memoria nei results.json.

- Ammissione 10/10:
  kernel/build/ipi-service-a15fab0ead3f42809b98909a70b0630a.
  Il runner legge il record scritto da AP1 e tutti i 704 byte dello slot
  locale; controlla owner, root, stack, stato iniziale e ritorno all'idle.
  Dieci richieste PING/flush per boot, prima/dopo l'ammissione.
- Ordinario 10/10:
  kernel/build/ap-runtime-97539af9e6b5432b86542b7df8fe6120.
- IPI con preservazione GPR/FPU/SIMD 10/10:
  kernel/build/ipi-service-0d4c5bc8b0b644d7b0edc6488df124df.
- Timer AP con stop confermato 10/10:
  kernel/build/ipi-service-600a4b71228445ad8746fd67b7ee6b09.
- Build, immagini e riepilogo 40/40:
  kernel/build/qemu-run-814e4ba23bf347a7b5ae150eb3279688.

Dopo la sola aggiunta di separatori al wrapper FFI, ricompilato l'oggetto
Rust con gli stessi flag: SHA-256 identico all'oggetto del diagnostico
collaudato. Evidenza in kernel/build/c4-admission-object-check/comparison.json.

Limiti: il punto di ammissione non chiama ancora il motore dispatcher.
La fase nativa AP resta zero; nessun task, timer scheduler o driver viene
eseguito su AP1. Il profilo ordinario mantiene lo scheduler BSP. Il test
positivo non dimostra timeout su hardware: la corsa terminale è provata
nei test host, non con un timeout QEMU iniettato. Coverage non misurata.
Restano quattro obiettivi C4: primo lotto AP, preemption AP, dispatcher
concorrenti e stress/regressioni di chiusura; poi C5 e C6.

## K4-C4 — Primo lotto reale su AP1

5 novembre 2026. Implementata la scelta utente: raccolta sul BSP dopo
quiescenza AP. Il diagnostico ap-batch esegue due task cooperativi su AP1,
sedici yield ciascuno e due exit, con checksum interi e floating point
verificati. Riceve PING e flush sugli stack task, poi ritorna all'idle.
Rust verifica autorità, token, transizioni e permesso di raccolta; C
possiede stack e mapping, lo switch usa il backend Assembly esistente.

Il motore retained conserva FINISHED e i due stack finché AP1 pubblica
QUIESCED fuori dal dispatcher. Il BSP verifica lo stato, revoca i mapping,
attende il flush remoto e infine esegue COLLECT. Sono trattenute 32 pagine
task (128 KiB); il budget iniziale viene ripristinato e le 36 entry,
comprese le guardie, risultano assenti. Un accesso tramite CPU errata
viene respinto. Non si autorizza il mapper a mutare risorse dall'AP.

### Prove host

- Core scheduler: test Rust debug/release, Clippy, C/FFI con ASan,
  undici modalità del motore, 16.837 controlli del routing su 64 identità
  e cinque stress da 20.000 task con quattro thread host.
  Evidenze: kernel/build/c4-batch-core-c7a123fe8527472288056307994beefa.
- Backend x86: contesto Assembly, ruoli BSP/AP, autorità stack e dati
  locali, warning come errori e analisi statica.
  Evidenze: kernel/build/c4-batch-backend-9adcb679823e46b994f71e550d2b4aec.
- Servizio AP e nuovo lotto: 19 test Rust debug e 19 release, Clippy,
  C/FFI ASan, 40 controlli del gate AP con letture privilegiate simulate,
  corse completamento/fallimento e rilascio/quiescenza, mailbox e runner.
  Ultima esecuzione sul sorgente finale:
  kernel/build/c4-batch-verified-host-3bdb7215e15248ce974eb1c775178ada.

### Campagne QEMU

Livello complesso: dieci boot consecutivi per profilo, QEMU TCG/OVMF,
quattro CPU inizializzate. Immagini ricompilate ed ESP/hash controllati;
il runner legge record, registri e mapping, non soltanto i marker seriali.

- Lotto AP1 finale 10/10 positivi:
  kernel/build/ipi-service-5d5e1cbcebca4c39a0185a2e8e08d301.
- Rollback finale 10/10 fault intenzionali:
  kernel/build/ipi-service-b30f696adf504d0aa3c76f04be48c7a6.
  L'iniezione precede la seconda allocazione: uno stack pronto viene
  rilasciato, due prenotazioni annullate e budget ripristinato prima
  del panic coordinato atteso. Non è una prova di esaurimento RAM.
- Build e riepilogo dei due profili finali:
  kernel/build/qemu-run-0d9e74ffb3284db28f5827323e8c4c21.
- Regressioni 10/10 ciascuna: ordinario, preemption BSP, ammissione AP,
  servizio IPI e timer AP. Cinquanta boot positivi nella campagna
  kernel/build/qemu-run-b24fd16ce9c64a01b0b19f931a00786e.
  Il lotto AP precedente presente nella stessa campagna è sostituito
  dalla prova finale sopra e non viene contato due volte.

Totale pertinente: 60 boot positivi e dieci negativi con panic atteso.
Nessun crash inatteso nelle campagne positive. Layout e diff verificati.
ASan riguarda gli eseguibili host; il recupero della RAM fisica è
verificato separatamente tramite accounting e mapping nel guest.
Coverage linee/branch non misurata; hardware fisico non provato.

Il boot ordinario mantiene lo scheduler BSP. Questo lotto è diagnostico,
finito e cooperativo: non dimostra preemption AP, migrazione o dispatcher
concorrenti. Restano questi ultimi obiettivi e lo stress di chiusura C4;
poi C5 driver SMP e C6 chiusura K4.

## K4-C4 — Protocollo del lotto concorrente e motore retained

5 novembre 2026. Primo blocco del contratto AP1/AP2 approvato:
implementati protocollo Rust, ABI C e percorso del motore C. Il
collegamento nativo di entrambi gli AP resta da fare; questo blocco non
dimostra ancora dispatcher concorrenti nel guest.

Il lotto vive dentro Owner, sotto il lock globale esistente. Registra
identità, generazioni, CPU partecipanti, ingressi, completamenti e
quiescenze. Solo il BSP raccoglie dopo tutte le quiescenze; conferme
parziali, token vecchi e doppie transizioni non autorizzano rilascio.
FAIL è terminale e conserva anche un current rimasto dopo un errore.
QUIESCE deve essere preceduto da controlli hardware nel futuro backend;
Rust controlla il protocollo, non misura autonomamente timer o stack.

Il motore concorrente conclude il proprio lotto locale con COMPLETE,
senza pretendere SELECT=EMPTY globale. Non pubblica QUIESCE e non libera
gli stack. Le API esclusive precedenti conservano il controllo EMPTY.

### Evidenze host definitive

kernel/build/scheduler-host-956a9e6d8e0c4127a7f0c480ae392ebd:

- 44 test dello stato Rust in debug e 44 in release, più suite binding
  e payload; warning come errori e Clippy passati.
- Dieci nuovi casi Rust: completamento parziale, duplicati, identità,
  rollback delle transizioni respinte, fallimento con current vivo,
  fallimento dopo raccolta parziale, corruzione, CPU 63 e token massimo.
- Fixture C/ASan con vero motore e vero core Rust: 100 lotti, 400 task,
  3.200 transizioni tick/preempt e 9.733 controlli. Due thread con
  identità CPU distinte; lock host SRW al posto del backend kernel.
  Il secondo thread resta nel primo callback finché il primo dispatcher
  non ha concluso: la conclusione con un altro current vivo è quindi
  obbligatoria, non affidata al caso. Non sono interrupt hardware.
- Negativo del motore: callback di esecuzione fallito, lotto FAILED,
  current conservato e raccolta negata. Nessun cleanup simulato spacciato
  per rilascio fisico.
- Regressioni C/ASan e cinque stress da 20.000 task su quattro thread;
  fixture preemption e CPU; cppcheck senza error, warning, performance
  o portability bloccanti. Style/information restano nel report XML.

Prime esecuzioni: Clippy ha respinto un if annidato nel controllo di
raccolta; corretto prima della suite definitiva, senza sopprimere il lint.

Owner passa da 2.312 a 2.600 byte (+288) sul target host x64 corrente;
misura e sorgenti temporanei in kernel/build/lot-core/sizes.log. Nessuna
nuova allocazione dinamica nel protocollo. ASan riguarda i fixture host;
Valgrind e copertura linee/branch non misurati. Il costo della validazione
del lotto attivo non è un benchmark del futuro percorso AP concorrente.

### Regressioni QEMU

Campagna passata: kernel/build/qemu-run-d1d98950fe424e51a6e9632041cb9ac7.
Livello complesso: 10/10 ap-task-preempt, 10/10 bsp-periodic e 10/10
ap-runtime; 30 boot positivi, nessun fault inatteso. QEMU TCG/OVMF,
quattro CPU inizializzate, immagini ricompilate e ESP/hash verificati
dai runner. Evidenze rispettivamente in kernel/build/ipi-service-0c3e857b4a2441ef89fc656afdcdad0f,
kernel/build/ap-runtime-63c91c8ef27841bca0b841f77c4cd9ac e
kernel/build/ap-runtime-b1ac680bd8864deabd23d94c5d1fb4af.

Il nuovo protocollo non è chiamato dai profili esistenti: i boot verdi
dimostrano regressioni e accounting dei percorsi già collegati. Non
provano il completamento o il cleanup hardware del nuovo lotto AP1/AP2.
Layout centralizzato e diff finale verificati. Nessuna nuova prova di
hardware fisico, migrazione o driver SMP. C4 resta aperta.

## K4-C4 — Primo lotto nativo concorrente AP1/AP2

5 novembre 2026. Il profilo TestApService ap-concurrent esegue quattro
task reali: due su AP1 e due su AP2, con affinità fissa e un solo Owner
Rust. Il BSP prepara, sorveglia e raccoglie; AP3 resta nel servizio idle.
Il boot ordinario dello scheduler resta BSP. Questo chiude il primo
collegamento concorrente, non lo stress finale di C4.

La barriera con acquire/release prova l'ingresso di entrambe le sessioni
nel workload prima dell'armamento dei timer. Ogni AP verifica tre
sospensioni e riprese per task, sei per AP, dodici complessive. Il
backend Assembly esistente controlla i pattern GPR e lo stato FPU/SIMD.
Gli IRQ PING e flush restano ricevibili durante il lavoro. Record IRQ,
timer, contesti e statistiche sono separati; nessuno stack dell'altro AP
può essere autorizzato dal gate corrente.

Ogni AP completa il lotto locale, verifica current assente, timer fermo,
ISR/IRR drenati e tutte le riprese concluse; poi pubblica la quiescenza
Rust e quella dell'ammissione. Il BSP controlla entrambe per entrambi gli
AP prima di revocare quattro stack, attendere gli shootdown, raccogliere
gli slot e chiudere il lotto. Le 64 pagine task tornano al budget iniziale.
Il runner legge record nativi, snapshot finale del lotto e 72 PTE assenti.

### Prove host

kernel/build/ipi-service-host-ba917eb9adf044b6a1be1ec9d0a01682,
log esterno kernel/build/ap-concurrent-host-final.log:

- 19 test Rust debug e 19 release, Clippy, mailbox e attese concorrenti.
- Gate precedente: 41 controlli; versione AP1/AP2: 87 controlli, inclusi
  GDT, identità e rifiuto di stack e frame dell'altra CPU.
- Preemption precedente: 125 controlli; due stati timer distinti:
  250 controlli. MMIO, switch e letture privilegiate sono simulati.
- Barriera e selettori dei record: 24 controlli C, con CPU, clock e
  atomiche simulate; identità errata, duplicati, input invalido e timeout.
- C compilato con warning come errori e ASan; cppcheck senza diagnostiche
  error/warning/performance/portability bloccanti. Le informazioni sugli
  header di sistema mancanti restano nel file XML: non è analisi esaustiva.
- Dodici test del runner, inclusa la verifica sintetica positiva dei
  record concorrenti e dieci mutazioni respinte (barriera, quiescenza,
  budget, identità IRQ, preemption, mapping e padding).

Debug iniziale: il nuovo record allineato richiedeva padding esplicito
per evitare C4324; aggiunto senza disabilitare il warning. Il fixture
del gate concorrente mancava del selettore del record IRQ simulato:
corretto prima della suite finale. Nessuna di queste prove fallite è
conteggiata come successo. Il vecchio formato del record resta invariato
nei profili precedenti.

### Campagna QEMU

Campagna definitiva passata: kernel/build/qemu-run-843e13ebbfab4704904c08c30d7375d7.
Livello complesso, dieci boot consecutivi per profilo, QEMU TCG/OVMF con
quattro CPU avviate, immagini ricompilate ed ESP/hash verificati.
Passati 10/10 per ap-task-concurrent, ap-task-preempt, ap-task-batch,
ap-dispatch-admission, bsp-periodic e ap-runtime: 60 boot positivi senza
fault inattesi. Separatamente, 10/10 ap-task-batch-rollback con panic
atteso e risorse ripristinate. I test negativi non sono boot positivi.
Il nuovo profilo ha evidenze in
kernel/build/ipi-service-4d4c587b14774a9cb3e378e2b76da963.
ELF SHA-256: ABE7BB9C084B3870F043A074C8B629395CA00F08DFECDFF40C3FA27D32A0626F.
Il risultato riporta TaskCpus=[1,2], quattro CPU realmente inizializzate
e 30 richieste IPI per boot oltre alla base del bootstrap.
Layout centralizzato e diff finale verificati; massimo 390 righe nei
sorgenti e script modificati, cartelle funzionali entro otto file.

Limiti: sessioni sovrapposte sotto TCG, non prova di esecuzione fisica
simultanea dei vCPU host. Nessuna migrazione o task sul BSP/AP3 nel nuovo
lotto. Il timeout della barriera è collaudato con mock host; non è stata
iniettata una mancata quiescenza AP nel guest. Il negativo QEMU di rollback
resta quello precedente, con errore iniettato prima della seconda
allocazione. ASan riguarda gli eseguibili host; l'accounting fisico e i
mapping sono verificati nel guest. Valgrind non eseguito, coverage N/D,
hardware fisico non provato. C4 resta aperta per lo stress di chiusura.

## K4-C4 — Preemption periodica su AP1

5 novembre 2026. Implementata la scelta A: AP1 termina autonomamente
il collaudo e ferma il timer prima della quiescenza. Il nuovo profilo
TestApService ap-preempt usa due task senza yield nella fase misurata:
tre sospensioni e tre riprese per ciascuna, sei in totale. Il timer usa
il reload BSP già calibrato, senza cambiare il timer o watchdog del BSP.

L'IRQ valida stack, generazione, RIP e sorgente, poi completa EOI.
Un hook nell'epilogo AP sospende la continuazione prima di FXRSTOR/IRETQ.
Il dispatcher applica tick/preempt al vero stato Rust per CPU 1 e riprende
il task. Il frame rimane nel suo stack pinned. PING/flush restano attivi;
il loro passaggio non consuma un tick dello scheduler. Dopo il terzo
ritorno ciascun task esce; il BSP raccoglie soltanto dopo QUIESCED.
L'eventuale attesa finale del permesso BSP resta cooperativa ed è fuori
dalla fase misurata senza yield.

Al sesto IRQ il timer viene mascherato e Initial Count azzerato.
Gli IRQ timer già pendenti vengono completati senza nuovi switch, con
limite diagnostico di otto. Prima della quiescenza si verificano count
fermo, LVT/divisore, ISR/IRR vuoti per 0xd1 e tutte le riprese concluse.
Il runner legge anche il record nativo da 128 byte, controlla i sei
completamenti e gli indirizzi storici dei frame nei rispettivi stack.
Il budget e gli unmapping seguono le stesse verifiche del lotto precedente.

### Prove host e oggetto Assembly

- Core: Rust debug/release e Clippy, C/ASan, regressioni del dispatcher,
  cinque stress concorrenti da 20.000 task. Il nuovo test C/Rust esegue
  4.231 controlli tick/preempt e raccolta retained su 64 identità
  sequenziali simulate. Evidenze definitive:
  kernel/build/scheduler-host-7fc09a6612e94a3a888f0b2381eaa54c.
- Servizio AP: 19 test Rust debug e 19 release, 40 controlli del gate,
  125 controlli della nuova logica IRQ, ASan e cppcheck passati. Nei
  nuovi controlli C, MMIO e switch sono simulati; non provano la CPU.
  Evidenze: kernel/build/ap-preempt-service-final-b8a5d1b2ac7b4b53854187186d5498bc.
- Oggetto NASM del diagnostico: salvataggio GPR/FXSAVE prima dei callback,
  hook xat_preempt_after dopo xis_native_dispatch, poi FXRSTOR e IRETQ.
  Disassemblato e relocation in kernel/build/ap-preempt-entry-audit.
  L'entry reale è verificata anche dal workload Assembly in QEMU:
  pattern dei registri generali e confronto dello stato FPU/SIMD salvato.

### Debug prima della campagna finale

Una prima prova non raggiungeva il diagnostico: il flag della preemption
BSP attivava anche il suo collaudo nel bootstrap. Il nuovo profilo usa un
flag AP distinto, riconosciuto dal motore senza abilitare il test BSP.
La prova successiva ha isolato un panic di lettura MMIO: il filtro del
servizio non ammetteva Initial/Current Count e Divide. L'estensione è
limitata al nuovo profilo, con Current Count in sola lettura e valori
LVT/divisore espliciti. Le prove fallite restano nelle rispettive evidenze;
non vengono contate come boot positivi.

Il test host iniziale aveva inoltre omesso il divisore nello stato MMIO
simulato; corretto il fixture prima della suite definitiva. Un tentativo
di fixture con un solo task è stato sostituito da due task: senza un peer
READY il tick Rust non richiede un cambio di task.

### Campagna finale

Campagna passata in kernel/build/qemu-run-ea96d156073d4e73b4cd37b15394b32e.
Livello complesso: dieci boot per profilo, QEMU TCG/OVMF con quattro CPU
inizializzate, immagini ricompilate ed ESP/hash verificati.
Passati 10/10 per ciascuno dei sette profili: ap-task-preempt,
ap-task-batch, ap-task-batch-rollback, ap-timer, bsp-periodic, ipi-service
e ap-runtime. Totale: 60 boot positivi senza crash inattesi e dieci
negativi con rollback e panic attesi. Le prove iniziali fallite non
sostituiscono e non entrano nel conteggio della campagna finale.

Preemption AP1: kernel/build/ipi-service-88c243df16524dbd9afc3fff5ce9eddc.
SHA-256 ELF: C23C5B2931269A5BFD43124216568A6AAAF5D0FDE990F3038F501942706BFF14.
Riletti anche i record dei dieci monitor salvati: tre tick e tre riprese
per task, Total=6, Stopped=1 e Late=0 in ogni boot. Riepilogo in
kernel/build/ap-preempt-entry-audit/records.json. Il ramo IRQ tardivi è
collaudato con mock host, non con un IRQ tardivo iniettato in QEMU.
Layout, dimensioni e diff finale verificati. Valgrind non eseguito;
non viene dichiarata una misura dei leak fisici tramite ASan.

Limiti: primo diagnostico AP1, non dispatcher concorrenti né migrazione.
Il reload resta un'ipotesi temporale calibrata, non una prova di jitter
o latenza universale. ASan misura gli eseguibili host; RAM fisica e
mapping vengono verificati nel guest. Coverage non misurata, hardware
fisico non provato. C4 resta aperta per concorrenza e stress finale;
poi C5 driver SMP e C6 chiusura K4.


## K4-C4 — Stress circoscritto e conferma Rust mancante

5 novembre 2026. Aggiunti due profili, senza nuove transizioni o accessi
hardware. Le scadenze preesistenti restano invariate. Il boot ordinario
non cambia; questi sono diagnostici del lotto AP1/AP2.

ap-concurrent-stress richiede otto sospensioni/riprese per ciascuno dei
quattro task: 32 per boot, 320 nei dieci boot. Otto giri PING/shootdown
portano il totale verificato a 65 richieste IPI per boot oltre alla base
iniziale del bootstrap. I record controllano anche identità, frame,
registri/FPU, arresto del timer e restituzione delle 64 pagine task.
Non misura throughput, jitter o latenza del futuro scheduler ordinario.

ap-concurrent-incomplete omette la sola conferma QUIESCE Rust di AP2,
dopo la sua verifica fisica. Entrambi gli AP confermano invece QUIESCED
nell'ammissione. Il BSP vede completed=6 e quiesced=2; un tentativo
COLLECT viene respinto. Il cleanup ordinario provoca poi il panic
coordinato prima del primo unmap. Il runner verifica tutte le 64 pagine
ancora mappate, quattro lease vive da 16 pagine ciascuna con pin=1,
guardie assenti e intervalli fisici distinti. Causa del panic e ACK NMI
di tutti i core devono coincidere con il caso previsto.

Non è un AP bloccato o ancora in esecuzione: il guasto riguarda una
conferma di protocollo mancante. Le risorse restano intenzionalmente
trattenute fino all'arresto, non vengono dichiarate rilasciate o perdute.

### Evidenze definitive

- Host: kernel/build/ipi-service-host-a2653183328440caba011b5e311bc91f,
  log kernel/build/k4c-stress-host.log. Suite Rust debug/release e Clippy,
  C con warning come errori e ASan, cppcheck senza diagnostiche bloccanti.
  Nuova variante stress: 470 controlli della logica IRQ su due identità
  simulate, oltre alle precedenti varianti e ai controlli del gate.
  MMIO e switch di questi fixture sono mock, non prove CPU.
- Runner: 15 test, inclusi rifiuto dei vecchi conteggi nel profilo stress,
  conservazione sintetica delle lease e cinque alterazioni del negativo
  respinte. Non è una misura di copertura linee o branch.
- QEMU: kernel/build/qemu-run-5ca66c03d13242caba7bdc6a0c43e767.
  Dieci boot per ciascuno di stress, concurrent e preemption AP1:
  30 boot positivi senza fault inattesi. Separatamente, dieci test del
  negativo incomplete con panic atteso e risorse trattenute. Quattro CPU
  avviate, task sulle sole AP1/AP2, QEMU TCG/OVMF e immagini/ESP verificate.
- Stress: kernel/build/ipi-service-77a610aad5fa457fb8e05d15cb2855e9.
  SHA-256 ELF: 236059EEDE4B1F5EDA629B381AFA1992DDDF158E4039A810F284EAC5CB6F1DFC.
- Conferma mancante: kernel/build/ipi-service-d1fc8b61436c4043bd2fbe91dc0db98d.
- Layout centralizzato e diff finale verificati. Le regressioni riguardano
  i profili modificati; i boot BSP/ordinari del blocco precedente restano
  evidenze delle rispettive immagini, non nuove prove di questo blocco.

ASan riguarda gli eseguibili host; accounting, pin e PTE sono controllati
nel guest. Valgrind non eseguito, coverage N/D, hardware fisico non provato.
C4 resta aperta: il carico nativo è ancora su due CPU, non quattro.


## K4-C4 — Chiusura del backend diagnostico su quattro AP

5 novembre 2026. Scelta A approvata e implementata: quattro AP di lavoro,
BSP supervisore, cinque vCPU totali. Otto task con affinità fissa, due
per AP; tutti gli otto slot disponibili sono usati. Un solo lotto per
boot, nessun driver migrato o riarmo. C4 è completata entro questo profilo
fidato; C5 e C6 restano aperte e il boot ordinario continua sul BSP.

Rust ora accetta da due a quattro AP, verifica maschere, capacità e due
task per partecipante, conserva i token e vieta qualsiasi raccolta prima
della quiescenza completa. Record, barriera e ammissione nativi hanno
stato distinto per AP. Ogni AP verifica il proprio contesto e i propri
stack; nessun lock o borrow Rust attraversa il context switch.

### Risultati

- ap-four: otto sospensioni/riprese per task, 64 per boot e 640 nei dieci
  boot; 116 richieste IPI per boot, oltre alla base del bootstrap. Le
  128 pagine task tornano al budget iniziale, con 144 PTE task/guardie
  assenti dopo raccolta e shootdown. Frame, GPR e stato FPU/SIMD sono
  controllati dal workload e dai record nativi dei quattro AP.
- ap-four-incomplete: AP4 omette la sola conferma Rust dopo la quiescenza
  fisica. completed=30, quiesced=14. COLLECT viene respinto e il cleanup
  ordinario entra nel panic previsto prima del primo unmap. Il runner
  verifica otto lease vive da 16 pagine, pin=1, mapping e guardie corretti,
  intervalli distinti e ACK NMI da tutti i quattro AP.

Il formato del record è 576 byte nel nuovo profilo; i formati precedenti
restano invariati. Il runner deriva gli indici dal numero di task e
rifiuta il profilo a quattro AP con meno di cinque CPU totali.

### Evidenze definitive

- Core: kernel/build/scheduler-host-c5875f178d8146a48861d4e7472c5595,
  log kernel/build/ap-four-core-host.log. 46 test Rust debug e 46 release,
  Clippy, C/ASan, cinque stress da 20.000 task e regressioni FFI/motore.
  Il modello prova lotti di due, tre e quattro partecipanti, selezione,
  preemption e raccolta negata finché manca l'ultima quiescenza.
- Backend: kernel/build/ipi-service-host-3b196097b90d4bac89fa87258874ee13,
  log kernel/build/ap-four-service-host.log. Nuovo gate: 173 controlli;
  preemption su quattro identità: 940; selettori e barriera: 80. CPU,
  MMIO, clock e switch dei fixture C sono mock. C con warning come errori,
  ASan e cppcheck passati; style/information restano nel relativo XML.
- Runner finale: kernel/build/ap-four-runner-host.log, 18 test passati.
  Include l'ultimo AP, l'ultima lease e il rifiuto della topologia errata.
- QEMU: kernel/build/qemu-run-c9f04c98cb5348ddb280e7d9ea11537e.
  Livello complesso: dieci boot per profilo, QEMU TCG/OVMF, immagini
  ricompilate ed ESP/hash verificati. Passati ap-task-four,
  ap-task-concurrent-stress, ap-task-preempt, bsp-periodic e ap-runtime:
  50 boot positivi senza crash inattesi. Separatamente, 10/10 per
  ap-task-four-incomplete e 10/10 per ap-task-concurrent-incomplete:
  20 negativi con panic e risorse trattenute previsti.
- Quattro AP: kernel/build/ipi-service-64e60a96d30b4a39a9b4239d5fd3be03;
  SHA-256 ELF F4967B1EF95CB80E415DDA18BB965C5D39EC2C4E2FE0C0A90BD159C1DC4CB906.
- Conferma AP4 assente: kernel/build/ipi-service-845983f412bf4a83870f9ebced4cc5c0;
  SHA-256 ELF F4C456060266A0FFCA0A9A927111645E332CFC2E232C136CA04D584EB191A790.

Review del perimetro: descrittori condivisi immutabili dopo il via,
contesti e record scritti dal solo AP proprietario, pubblicazione con
acquire/release, stato scheduler sotto guard Rust, mutazioni paging sul
solo BSP dopo tutte le quiescenze. I task di calcolo non accedono ai driver.
Audit G5 globale e sicurezza SMP dei driver restano C6/C5, non risultati
di questa revisione circoscritta. Layout e diff finale verificati.

Limiti: il test nativo usa due o quattro AP; tre partecipanti sono provati
nel modello Rust. Sotto TCG si dimostra sovrapposizione delle sessioni,
non parallelismo fisico dei vCPU host. Non sono benchmark di jitter o
throughput, né stress di lunga durata; nessun AP fisicamente bloccato
viene iniettato. ASan riguarda i programmi host, non i leak fisici.
Accounting e pin sono osservati nel guest; Valgrind non eseguito,
coverage N/D e hardware reale non provato. I checkpoint precedenti
restano evidenze delle proprie immagini, non dello stato corrente.

## K4-C4 — Review: hook di uscita IRQ e costanti

Dopo la review, prima della chiusura di C4.

- **Hook unico all'uscita dell'IRQ.** `return.asm` (BSP) e
  `interrupt.asm` (AP) chiamano sempre `x86_irq_after` e
  `xis_irq_after`, dopo il dispatch e prima di FXRSTOR/IRETQ. La scelta
  del comportamento diagnostico è in un solo punto C; senza profili di
  preemption l'hook non fa nulla. È il punto di aggancio della
  preemption ordinaria di C6.
- **Comando PREEMPT del motore sempre disponibile.** Prima era compilato
  solo nei profili diagnostici; resta raggiungibile solo dagli hook di
  preemption.
- **Costanti con nome.** `interrupts/apic/lvt.h` definisce maschera LVT,
  modalità periodica, placeholder mascherato e vettore del timer BSP;
  `XIS_TIMER_VECTOR` è l'unica definizione del vettore del timer AP.
  Sostituiti i valori scritti a mano nei file di C2–C4.

## K4-C5 — Binding comune e worker BSP in WAIT/HLT, 6 novembre 2026

Risultato: binding comune BSP/AP attivo in tutti i profili; nel diagnostico
`ap-console-ipi` un vero task worker sul BSP attende l'evento 7 e viene
risvegliato dopo un HLT interrotto dall'IPI 0x43. C5 resta aperta per la
protezione dei driver e l'integrazione nella console ordinaria.

| Gate | Risultato | Evidenza in `kernel/build/` |
| --- | --- | --- |
| Core Rust | 47/47 debug e release, nuovo test `foreign_affinity_is_empty` | suite `scheduler` |
| Suite host | scheduler, x86-scheduler, ipi-service, ap-admission, ap-runtime, console, console-requests, x86-framebuffer, ps2, watchdog, hpet, pointer, layout, apic-icr, percpu-panic: tutte PASS | `test-run-*` |
| Worker positivo | 10/10 boot, 2.000 WRITE in ordine per boot | `ap-console-29ddc6cd24b54f6e9717006454748e61` |
| Ack mancante | 10/10 arresti attesi, chiusura negata | `ap-console-27ebbb287c8140fd9e964184191e8581` |
| Invio in corso | 10/10 arresti attesi, chiusura negata | `ap-console-59df01d77f3240af9f060edb5e7a598e` |
| Regressioni a 10 boot | ap-runtime, shell-text, bsp-periodic, bsp-preemption, ap-task-preempt, ap-task-four, ap-console | `qemu-run-74324d7695064b16ad9aec9af892c9f7` |
| Regressioni a 3 boot | ap-task-batch, -concurrent, -concurrent-stress, -batch-rollback, -four-incomplete, -concurrent-incomplete, ap-console-incomplete, console-active, v2c, ipi-service, ap-runtime-bsp, ap-timer | `qemu-run-bd6855c67f074aeea3c4ae336acad779` |

Numeri del worker sui 10 boot positivi: 124-141 WAIT per boot, tutte
risvegliate (`Woken` = `WAIT`), ogni idle chiuso con EVENT e con almeno un
HLT interrotto dall'IPI; 610-640 passi del dispatcher BSP; 617-747 IPI
inviati, consegnati e chiusi con EOI. Questi valori non sono una misura di
latenza: ogni attesa dura fino alla scadenza HPET di 5 ms.

ELF positivo SHA-256: `10EB0777ADCFB666BFB9513CED14A7176FCE60A506751C3DEF9A608AF1B4F77D`.
QEMU q35/TCG/OVMF, quattro CPU. Nessuna prova su hardware fisico.

Limiti noti: `kernel-boot` fallisce già sul commit di partenza (inventario
paging diverso dalla fotografia), indipendente da questo lavoro. Il test
`ps2/shell/bridge_test.c` è stato allineato all'ordine SUBMIT-prima-del-push
introdotto in 177dcab.

## K4-C5 — Driver solo su CPU 0, 6 novembre 2026

Scelta dell'utente: owner unico BSP, imposto dal codice. I 15 punti
d'ingresso di PS/2, shell, console e framebuffer/VirtIO usano
`x86_scheduler_driver_task_valid()`: task valido, assegnato alla CPU 0 e in
esecuzione sulla CPU 0. Il watchdog rifiuta già gli AP tramite il confronto
di `IA32_APIC_BASE` con il profilo BSP.

Prova in guest: nel diagnostico `ap-console-ipi` i produttori su AP1 hanno
task valido ma controllo driver negato; il worker BSP li ha entrambi.
Suite host ps2, console, console-requests, x86-framebuffer, x86-scheduler,
pointer e layout PASS. QEMU: v2c, shell-text e ap-console-ipi 10/10 boot
(`qemu-run-debf8b7543a44943ad61d21278e355a2`); console-active, ap-console,
ap-runtime, ap-console-ipi-no-ack e fallback 3/3
(`qemu-run-953fb45aba6e4adaa57d1de48a257abd`). `kernel.c` non ha un harness
host eseguibile: la regola è provata solo in guest.

## K4-C5 — `ap` nella shell ordinaria, 7 novembre 2026

Il riproduttore GOP ha catturato il BSP fermo con `RAX=0x4b495251554e0005`
e vettore `0x43` in servizio. Il RIP interrotto era dentro la finestra IRQ
del worker PS/2. Il filtro del frame respingeva il vettore prima che
l'handler potesse consegnare la notifica. Evidenza in
`kernel/build/c5-debug-gop-f80da89dbdd74306989d54fec4a2a7fd/monitor.log`.

Il cleanup del canale ora avviene prima che il dispatcher lasci lo
scheduler. Il test host copre anche la chiusura con zero consegne.
Il percorso a una CPU conserva la configurazione BSP e arriva a
`KERNEL_READY` dopo `ap 1000` respinto, `help` e `exit`.
Evidenza in `kernel/build/c5-bsp-final-4c19bd9a538e4f868e8de5291fe40027`.

Nel primo `v2c` la shell ha riportato CPU 1, somma 500500, checksum
`12bac6d7386781cd` e cleanup riuscito. Il runner ha respinto il contatore
dei lock di AP1 avanzato da 1 a 12, sebbene profondità e frame fossero a
zero. L'attestazione verifica ora il contatore avanzato per la sola CPU
che ha eseguito il job e conserva il valore 1 per gli altri AP.

| Gate | Risultato | Evidenza in `kernel/build/` |
| --- | --- | --- |
| `v2c` finale, QEMU q35/TCG/OVMF, 4 CPU | 10/10 consecutivi. CPU 1, somma 500500, checksum esatto, una WAIT della shell e ritorno di AP1 in HLT in ogni boot | `pointer-interactive-check-88f33e0a3e36418aa8fe0a46e7ddd0a1/results.json` |
| Cleanup `v2c` | 10/10, budget 14.093 pagine prima e dopo, 3 shootdown nel teardown VirtIO per boot | stesso report |
| `shell-text`, `ipi-service`, `ap-runtime` | 3/3 per profilo | `qemu-run-be491cb6ce704db49ba387426cad6fe5/results.json` |
| `ap-console-ipi` | 3/3 | `qemu-run-5f22cc06837a470a99cd7eb2847b5278/results.json` |
| Host | PS/2 e shell C/Rust, console, console-requests e IPI-service PASS. ASan nei test host C, hardware e parti dello scheduler modellati | `ps2-decoder-4b34b7b6ca16426c9a96e539baf493d6`, `console-grid-host`, `console-requests-362e603cd7254baeae912258e60c15ab`, `ipi-service-host-acf65b5a0f93431b8cdd794f99973cb8` |
| Statico | Cppcheck sul profilo AP senza diagnostiche bloccanti e layout PASS | `c5-static-39821c74c84f43fc80918a895e9812c1` |

SHA-256 ELF `82DF3336B09F855ABA3E4397E91BA70B135F2BA72D90E4BEC55314EF77344AC0`.
SHA-256 loader `37F4DE1FCDB792D2684D1F1C8B37F7EFD8DB75F0CA47AE3B7887088BAB119854`.
C5 chiusa per il profilo ordinario con un job fidato alla volta su AP1.
Lo stress e l'audit di chiusura G5 restano in C6. Hardware fisico e
leak detector nativo non verificati.
