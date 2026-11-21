# Validazione del consolidamento

Versione completa precedente in `docs/status.md`, `docs/review/verified-state.md`, `docs/review/status-2026-10-22.md`, `docs/handoff/codex-status.md`, `docs/handoff/k4-remaining.md`, `docs/design/k1-higher-half.md`, `docs/design/project-direction.md` e `docs/design/k6-design.md` (build), al commit `efecb8b`.

La validazione di chiusura dell'immagine unica è aperta. I conteggi
seguenti appartengono alle immagini e alle revisioni indicate nei
report. Lo [stato](../status.md) descrive l'immagine ordinaria attuale.

## Boot, memoria e dispositivi

| Checkpoint | Prova e perimetro |
| --- | --- |
| Loader, 2 ottobre | EFI di 54.272 byte, SHA-256 `FF6EB0D78AF37328394FE015171260577DB225E4CC23BC29996AF933C1AC10C1`. Test host C/C++ e Rust, 5000 mutazioni ELF e 512 sequenze di errore delle tabelle. QEMU/EDK2 10/10 casi loader. Il kernel ordinario non veniva eseguito e il cambio CR3 era fuori prova. |
| Dispatcher, 8 ottobre | 10 boot positivi e 9 negativi su QEMU TCG/OVMF. Scheduler cooperativo BSP, IRQ durante il worker, epoca e idle riusato. [Report](../../kernel/scheduler/idle/documentation/validation.md). |
| Boot e console, snapshot 22 ottobre | Bootstrap di memoria separato con 10 boot per profilo e 23 scenari diagnostici. Shell e input PS/2 in profili separati, console grafica GOP e prove VirtIO per componenti. [Bootstrap](../../kernel/bootstrap/documentation/memory.md), [PS/2](../../arch/x86/kernel/drivers/input/ps2/documentation/validation.md), [shell](../../kernel/shell/documentation/shell.md), [presentazione VirtIO](../../kernel/drivers/video/virtio/resources/documentation/validation.md). |
| K1, revoca dello slot basso | Dieci boot dedicati e negativo con traduzione scaldata prima di MOV CR3. Kernel 23/23, campagne brevi 12/12, `v2c` 10/10 e 37 suite host complessive con retry mirato. Le pagine del loader restano riservate. [Report](../../arch/x86/mm/paging/retirement/documentation/validation.md). |
| K2, lock, pool e heap | Ticket lock con 140.000 acquisizioni FIFO host. Integrazione ordinaria 17/17 smoke, più dieci boot dell'ELF heap ricompilato. Contesa host con thread e ASan. Il runtime guest di questo checkpoint era BSP. [Report](../../kernel/sync/documentation/validation.md). |

Per le verifiche puntuali dei driver del checkpoint di ottobre valgono
i report di [ECAM](../../arch/x86/kernel/drivers/bus/pci/ecam/documentation/validation.md),
[handoff PCI](../../arch/x86/boot/uefi/pci/documentation/validation.md),
[discovery VT-d](../../arch/x86/kernel/drivers/memory/iommu/intel/discovery/documentation/validation.md),
[domini VT-d](../../arch/x86/kernel/drivers/memory/iommu/intel/domains/documentation/validation.md)
e [code DMA VirtIO](../../kernel/drivers/video/virtio/queues/documentation/validation.md).

## CPU e scheduler

- **K3.** GS, clock delle attese, identità e risorse AP precedono
  ammissione, sigillo e avvio. K3-F aveva un solo core OS attivo anche
  con quattro vCPU configurate. K3-H provò la root inattiva e il
  rollback. K3-I avviò realmente BSP e tre AP e verificò il panic
  coordinato con origine BSP e AP. Chiusura 80/80 su QEMU/OVMF xAPIC,
  30 boot ordinari/console e 50 panic intenzionali, più una sessione
  continua di circa 301 s. [K3-F](#checkpoint-per-cpu),
  [K3-H](#checkpoint-per-cpu),
  [chiusura K3-I](consolidamento.md#validazione-dei-backend-x86).
- **K4-A/B.** PING agli AP nel boot ordinario, 40 boot su 1/2/4/8
  CPU. Shootdown prima del riuso dei frame, 40/40 a quattro CPU;
  ACK mancante con risorsa trattenuta nel caso diagnostico.
  [K4-A](consolidamento.md#validazione-dei-backend-x86),
  [K4-B](consolidamento.md#validazione-dei-backend-x86).
- **K4-C.** Il core Rust fu verificato in host e la preemption timer
  nei profili BSP/AP. `ap-task-four` provò otto task su quattro AP
  con BSP supervisore, 50 boot positivi e 20 arresti attesi per
  quiescenza incompleta. `v2c` passò 10/10 a quattro CPU e
  un soak VirtIO/VT-d di 301 s. Il comando `ap` resta della shell
  separata e i driver restano sul BSP.
  [Report K4-C](../../kernel/scheduler/documentation/validation.md).
- **3d-1.** Sull'immagine ordinaria Meson, QEMU TCG con quattro vCPU:
  10/10 boot di `--ordinary-shell` e un `--ordinary-soak 30` con
  311 richieste `bridge status`, uscita della shell e rilascio delle
  risorse confermati. Sforamenti della preemption e recuperi HPET: zero.
  Suite host superate: watchdog, kernel, scheduler, x86-scheduler, ps2,
  ps2-mouse, console, build-plan. Il boot ordinario esercita le transizioni
  da task che partono a IF=0. Ingresso nelle transizioni con IF=1 e
  hardware fisico non provati. [Design](../design/scheduler-produzione.md#3d-1--if-durante-le-transizioni-dello-scheduler).

- **3d-2.** Test host del predicato con frame simulati per i quattro
  task BSP. Il primo frame valido fuori dalla finestra falliva con il
  codice 3d-1. Suite host `ps2`, `ps2-mouse`, `scheduler`,
  `x86-scheduler` e test Meson `plan_parity` superati. Sull'immagine
  ordinaria Meson con quattro vCPU QEMU TCG, 10/10 boot
  `--ordinary-shell` e stress di cinque minuti con 52 richieste
  `bridge status`, uscita pulita, zero sforamenti e zero recuperi HPET.
  Frame fuori dalla finestra in QEMU e PC fisico non provati.
  [Design](../design/scheduler-produzione.md#3d-2--ammissione-del-frame-irq-di-un-task-bsp).

- **3d-3.** Suite host `ps2`, `ps2-mouse`, `console`,
  `kernel`, `scheduler`, `x86-scheduler` e `plan_parity` superati.
  Il test dello scanner si arresta a ogni confine e rifiuta un alias.
  Il test del worker simula cambi indipendenti delle due generazioni
  e verifica il riavvio. Il profilo diagnostico `paging-tamper` rileva
  la corruzione in 3/3 boot. Immagine ordinaria Meson con quattro
  vCPU QEMU TCG: 10/10 boot `--ordinary-shell` e cinque minuti di
  stress con 52 risposte del bridge, 65 audit completati, uscita
  pulita, zero sforamenti e zero recuperi HPET. Il tempo totale
  massimo di un audit è circa 146 ms, comprese le cessioni al
  dispatcher. Durata massima a IF=0, task avviati con IF=1 e
  hardware fisico non verificati.
  [Design](../design/scheduler-produzione.md#3d-3--dati-condivisi-con-gli-irq).

- **3d-4.** Otto suite host superate, `plan_parity` 1/1 e test del
  runner 2/2. Dopo la correzione del testo di stato della shell, suite
  PS/2 e mouse ripetute con esito positivo. Sull'ELF ordinario finale
  `70E4E885D943F6C0BE0CEA6F5ACB7E521AC1E285316E2102C6D627AF8546241F`,
  QEMU TCG con quattro vCPU: 10/10 boot della shell, con 122–147
  sospensioni fuori finestra per boot e zero sforamenti. Un boot
  interattivo verifica mouse, cancellazione, job su AP1 e budget
  finale invariato. La prima esecuzione falliva perché il test
  richiedeva almeno un'attesa anche con risultato AP già disponibile.
  Rimosso quel vincolo, mantenuti i controlli del risultato e cleanup.
  Soak di 30 minuti passato: 310 richieste al bridge, 11.186 sospensioni
  fuori finestra, 372 audit, zero sforamenti e zero recuperi HPET,
  uscita pulita. Massimo misurato di una porzione audit a IF=0:
  66.951 tick PM, circa 18,7 ms. Il dato supera il quanto nominale
  di 10 ms e non dimostra un limite massimo di latenza. Il contatore
  degli sforamenti riguarda il rinvio per riga seriale aperta.
  `paging-tamper` 3/3 su profilo diagnostico. Hardware fisico
  e preemption ordinaria degli AP non provati.
  [Design](../design/scheduler-produzione.md#3d-4--preemption-fuori-dalla-finestra-di-attesa).

- **3e.** Otto suite host superate, con ripetizione di `build-plan`
  dopo l'allineamento del messaggio di rifiuto del profilo ritirato.
  Runner Meson 2/2, con casi positivi e negativi del gate sui contatori;
  `plan_parity` 1/1. Quattordici campagne completate, 77 boot riusciti:
  `k6-ordinary`, `k6-shell`, `k6-no-ps2` e `k6-process` 10 ciascuna,
  bridge disabilitato e bridge fault 3 ciascuna, `plain` 3, `v2c` 10,
  `k3-soak` 1, fallback e absent 2 ciascuna, stall 5, paging-tamper 3,
  `meson-bsp-periodic` 5. I casi video, fault e periodico restano
  diagnostici. La prima prova periodica richiedeva ancora il marker
  ritirato: corretto il controllo e ripetuti tutti i cinque boot.
  Le esecuzioni non concluse per interruzione della sessione non sono
  incluse nei 77 boot.
  Soak QEMU TCG ordinario con quattro vCPU, sullo stesso ELF finale
  della 3d-4: 30 minuti, 311 risposte del bridge, 19.340 sospensioni
  totali, 14.168 fuori finestra, zero sforamenti, 521 audit, zero
  recuperi HPET e cleanup confermato. `K4TLBACK` non riprodotto.
  Massimo campionato per porzione audit a IF=0 circa 13,5 ms.
  Il limite di latenza già riportato nello stato resta aperto.
  Preemption ordinaria AP e hardware fisico non verificati.
  Evidenze locali: `qemu-run-84f32efd12c64006a6d619d9358df0c3`,
  `qemu-run-e5cbaf39bfc94fc491357f14d6187814`,
  `qemu-run-d2759cb60f174c8fa795a1dfd2184a6c` e `qemu-3e-soak`,
  sotto `kernel/build/`.
  [Design](../design/scheduler-produzione.md#3e--chiusura-delle-prove-bsp).

- **B2-1, 19 novembre 2026.** Separata la preparazione di timer/watchdog
  dall'esecuzione di init e della sessione PS/2, con contesto posseduto
  dal chiamante fino al cleanup. Test Meson `watchdog_integration` 1/1
  con ASan e warning come errori: otto scenari verificano ordine,
  ambiente conservato, directory/topologia mancanti, preparazione
  fallita, timer indisponibile e propagazione dell'errore di cleanup.
  Timer, processo e PS/2 sono sostituiti nel test host: la prova riguarda
  l'orchestrazione. Cppcheck 2.22.0 sul file di integrazione senza
  diagnostiche bloccanti. Suite host watchdog, x86-scheduler, scheduler
  e PS/2 4/4; `plan_parity` 1/1, runner Meson 2/2. Il primo tentativo
  era privo di cppcheck; dipendenza recuperata dalla release ufficiale.
  Le esecuzioni Meson rimaste in attesa nel sandbox sono state chiuse
  e ripetute fuori dal sandbox. Il test ASan usa il runtime del
  compilatore MSVC rilevato da Meson.
  Build di kernel e loader nel checkout pubblico `D:\VoidOS-public`.
  `ordinary` su QEMU/OVMF, TCG/q35, quattro vCPU: shell con bridge e
  cleanup 10/10, controller PS/2 assente con init e idle a IF=1 10/10.
  Soak di cinque minuti 1/1: 52 risposte bridge, 2.907 sospensioni totali,
  2.204 fuori finestra, zero sforamenti, 85 audit, zero recuperi HPET
  e cleanup completato. Nei dieci boot shell: 144-172 sospensioni fuori
  finestra e 211-217 totali per boot, zero sforamenti.
  Kernel SHA-256 `C384F91197F560A1F5063A372FF909E968ED181034B0D58B02FDD54F4EAB4242`.
  Loader SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
  Evidenze in `kernel/build/b2-1-shell`, `b2-1-no-ps2`, `b2-1-soak`,
  `meson-watchdog-integration`, `meson-qemu-runner` e
  `test-run-0d7782aa2352411b95dee6979d04c575`.
  Il test di orchestrazione si configura con
  `meson setup kernel/build/meson-watchdog-integration test/arch/x86/kernel/timer/hpet/watchdog --vsenv`
  e si esegue con `meson test -C kernel/build/meson-watchdog-integration`.
  Il dispatcher permanente e il cleanup PS/2 con dispatcher ancora
  attivo appartengono ai task successivi. Hardware fisico e preemption
  ordinaria AP non verificati in questo task.

- **B2-2, 19 novembre 2026.** Il dispatcher BSP usa il motore continuo
  e rimane operativo dopo la raccolta della sessione PS/2 o in assenza
  del controller. Il runtime conserva timer, watchdog e servizio bridge.
  Il runner richiede due campioni completi con cicli e tick crescenti,
  pagine libere invariate e IF=1 durante l'attesa. Dopo `exit` confronta
  anche le pagine con il budget precedente alla creazione dei task.
  `ordinary` QEMU/OVMF TCG/q35, stesso ELF: shell 10/10, assenza PS/2
  10/10, SMAP assente 3/3, shell con una vCPU 1/1 e con otto vCPU 1/1.
  Nei dieci boot shell: 151-198 sospensioni IRQ fuori finestra,
  166-236 sospensioni complessive per boot e zero sforamenti.
  Soak 1/1 sullo stesso ELF: cinque minuti nella shell con 52 risposte
  bridge, 5.046 sospensioni totali, 4.252 IRQ fuori finestra, 85 audit e
  zero sforamenti. Dopo `exit`, altri cinque minuti con 41.200 cicli del
  dispatcher, 58.128 tick LAPIC complessivi e 16.172 pagine libere
  invariate. Cleanup PS/2 confermato prima dell'attesa continua.
  Sei suite host superate: scheduler, x86-scheduler, watchdog, PS/2,
  richieste console e contratto kernel. Diagnostica con ASan superata.
  Meson integrazione/log 2/2, runner 2/2 e `plan_parity` 1/1.
  La regressione del motore copre zero task, raccolta, completamento una
  sola volta, rientro rifiutato, callback fallito e EMPTY prematuro.
  La regressione della preemption copre mille righe completate con un
  tick rinviato ciascuna; resta verificato lo sforamento di una riga
  che rimane aperta oltre il limite. Il test del log usa la seriale
  simulata; il test del canale AP usa la FFI Rust reale con modelli di
  hardware, registro IRQ e scheduler, incluso il risveglio di altri task.
  Cppcheck 2.22.0 superato con le soppressioni già previste per le stringhe
  del log. Il codice comune del log usa ora interfacce dichiarate in
  `kernel/diagnostics/log.h`, con backend in `arch/x86`.
  Il primo boot falliva perché il callback riceveva `stats.failure=1`:
  corretto e aggiunta regressione. Una campagna successiva si fermava a
  3/10 per uno sforamento; ora il task soddisfa la preemption pendente
  alla chiusura della riga, senza aumentare il limite di quattro tick.
  Il primo soak falliva nella chiusura AP con failure 6 del dispatcher.
  Il canale in chiusura ora accetta risvegli validi di altri task; il
  percorso diagnostico conserva il controllo sui risvegli inattesi.
  Il log originario non distingueva il singolo passo del cleanup;
  sono stati aggiunti i motivi distinti per canale e servizio remoto.
  Una fixture del runner usava numeri esadecimali corti: allineata al
  formato reale a sedici cifre. Una compilazione del runner in uso è
  fallita per il lock dell'eseguibile Windows; ripetuta a runner chiuso.
  Le campagne preliminari interrotte e le immagini precedenti sono
  escluse dai conteggi conclusivi.
  Kernel SHA-256 `9F6DA47B48075FAE7E763EF4750F71D450AB17AD87807470EFB9079CCDD13ED2`.
  Loader SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
  Evidenze sotto `kernel/build/b2-2-corrected-*`,
  `test-run-eb05297d90f44bb8bfed53ab4d656977`,
  `test-run-4352b870782f4576a51b0a4ad6b4658e` e `b2-2-diagnostics`.
  Restano B2-3, preemption ordinaria AP e prove su hardware fisico.

- **B2-3, 19 novembre 2026.** B2 e l'obiettivo BSP di S1 sono fatti.
  Riletti il diff complessivo di B2 e i percorsi di ownership e raccolta.
  Il runner esegue `ap 1000` nei casi shell con almeno due vCPU e
  verifica CPU 1, somma 500.500 e raccolta del job prima del cleanup PS/2.
  Legge dalla memoria della VM i 240 byte del registro IRQ, secondo
  `arch/x86/kernel/interrupts/registry/registry.h`. Tastiera, mouse e
  notifica AP devono avere i tre slot completamente azzerati; HPET e
  timer D1h restano sottoscritti. I controlli nativi verificano task,
  stack, PTE, payload, binding e budget fisico della sessione.
  Sull'immagine `ordinary` QEMU/OVMF TCG/q35 finale, i 25/25 boot ripetuti
  con il runner corretto comprendono shell 10/10, controller PS/2 assente 10/10,
  SMAP assente 3/3, una vCPU 1/1 e otto vCPU 1/1. Quattordici job AP
  verificati. Nei dieci boot shell, 174–201 sospensioni IRQ fuori
  finestra e 258–264 complessive per boot, zero sforamenti.
  Lo stress conclusivo 1/1 sulla stessa immagine con quattro vCPU ha
  completato quindici minuti nella shell con 156 risposte bridge, 9.750 sospensioni
  complessive, 7.205 fuori finestra, 286 audit e zero sforamenti.
  Dopo `exit`, quindici minuti di dispatcher con 119.200 cicli,
  171.589 tick LAPIC complessivi e 16.172 pagine libere invariate.
  Il budget prima e dopo la sessione è 16.172. Snapshot finale IRQ
  con tre slot liberi, sottoscrizione HPET 2 e timer 6. Nessun recupero
  HPET segnalato. La VM posseduta è stata terminata dal runner a prova
  conclusa; il sistema ospite continuava nel dispatcher.
  Le sei suite host superate comprendono scheduler, x86-scheduler,
  watchdog, PS/2, richieste console e contratto kernel. PS/2 ripetuta dopo la correzione
  della shell. Meson integrazione/log con ASan 2/2, `plan_parity` 1/1.
  Runner Meson 2/2, con 17 test unitari e sei test di processo.
  Il test Windows verifica richiesta di esecuzione e ripristino dello
  stato del thread; i test del monitor coprono dump incompleti e invalidi.
  Cppcheck 2.22.0 sulla shell senza diagnostiche bloccanti.
  La prima campagna falliva su `SHELL_AP_SUM`, poiché il callback video
  poteva cedere il BSP fra etichetta e numero seriale. La shell ora
  completa le righe numeriche e di `echo` prima dei callback video.
  La regressione host inserisce output concorrente da quel callback.
  Un successivo soak scadeva 40 secondi dopo l'ultimo campione delle
  21:37:41. Windows registra sospensione per inattività alle 21:37:42
  e riattivazione alle 21:37:45. Il runner ora mantiene una richiesta
  temporanea di esecuzione sul proprio thread durante la campagna e
  ripristina lo stato precedente all'uscita. Nel test conclusivo non
  risultano nuovi eventi di sospensione. [API Windows](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate).
  Il gate IF/HLT di un altro tentativo usava due richieste distinte al
  monitor e falliva, sebbene 58 dei 477 snapshot mostrassero già IF=1 e HLT=1
  insieme. Il gate ora richiede entrambi nello stesso snapshot.
  I 25 boot conclusivi sono stati ripetuti dopo questa correzione.
  Prove fallite, immagini precedenti e stress interrotti sono esclusi
  dai conteggi conclusivi. Gli snapshot diagnostici vengono raccolti
  dal monitor prima della chiusura anche quando una prova fallisce.
  Kernel SHA-256 `36C2995FDE351EBC4DE29942348D784F03F2E5646322E056F77E026E44D03D71`.
  Loader SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
  Runner SHA-256 `7EC7C9823359A3FA4B8D9FB8C9E3B3DA255236CD910077FD13A830298B9E5E60`.
  Evidenze in `kernel/build/b2-3-conclusive-*`, `meson-qemu-runner`,
  `meson-watchdog-integration`, `test-run-c20af78c3e4543a0b4eb6d3d4664ee9c`
  e `test-run-5a93871b39e74245b799e209e1edce18`. Eventi Windows della prova
  interrotta in `kernel/build/b2-3-final-soak/host-sleep.json`.
  Restano preemption ordinaria AP, capacità e hardware fisico.
  Il dubbio storico `K4TLBACK` e la latenza a IF=0 restano nello stato.

- **Ingressi IRQ comuni, 20 novembre 2026.** Task preparatoria del runtime
  AP ordinario, scelta B approvata dall'utente. Un solo ingresso Assembly,
  IDT e dispatch per BSP/AP, registri e token selezionati tramite GS.
  Rimossa l'IDT duplicata AP. Ogni registro contiene 224 posizioni per i
  vettori architetturali 32–255, con F0h e FFh riservati, quindi 222
  sottoscrizioni simultanee. La numerazione segue Intel SDM 253668-085US,
  Vol. 3A, §7.2. [Fonte Intel](https://cdrdv2-public.intel.com/835754/253668-sdm-vol-3a.pdf).
  Il test host collega il registro C reale e
  il validatore Rust, sottoscrive tutti i vettori consentiti, consegna un
  interrupt logico a ciascuno e li rilascia. Questa prova host non esegue
  istruzioni privilegiate. ASan sul registro reale superato.
  Le cinque suite host kernel, ipi-service, build-plan, x86-scheduler e
  watchdog sono passate. Il primo collegamento della suite kernel
  falliva dopo l'ampliamento delle strutture. Il fixture grande è ora
  statico e la DLL usa gli helper di memoria reali del runtime.
  Suite kernel ripetuta con esito positivo. Runner Meson 2/2, con 17 test
  unitari e sei di processo, e `plan_parity` 1/1. Cppcheck sui cinque
  sorgenti C modificati senza diagnostiche bloccanti.
  `ordinary` Meson, QEMU/OVMF TCG/q35, kernel SHA-256
  `67B59CB4F414928B9A48CDBD6DAC9C3F07BC20FD12E24E199F6D79477C445499`.
  Loader SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
  Boot conclusivi 22/22, shell 10/10, PS/2 assente 10/10, una vCPU 1/1
  e otto vCPU 1/1. Nei casi shell con più CPU, il runner verifica il job AP1
  e il cleanup PS/2. Legge l'IDTR effettivo di tutte le CPU e il registro IRQ
  privato di ogni AP. Restano HPET e timer BSP, un IPI E1h per AP e tre slot PS/2
  azzerati. A quattro vCPU le pagine fisiche libere sono 16.033.
  La riserva iniziale IRQ è 581.632 byte, prima dell'heap, con record di
  9.088 byte allineati a 64 e registro di 9.000 byte per CPU. La variazione
  rispetto alle 16.172 pagine di B2-3 deriva dall'immagine ampliata.
  I profili separati `meson-bsp-periodic`, `ap-timer`, `ap-preempt` e
  `ap-four` passano 3/3 ciascuno. Gli ultimi tre sono stati ripetuti sulle
  stesse immagini dopo aver aggiornato l'asserzione IDTR del checker,
  che richiedeva ancora l'indirizzo dell'IDT AP rimossa. Il controllo
  verifica ora l'IDT comune anche sul BSP e conserva le prove TR, GDT,
  stack, root e registri salvati. Le tre campagne precedenti si erano
  fermate al primo boot e sono escluse dal conteggio conclusivo.
  Evidenze in `kernel/build/irq-unification-*`,
  `test-run-2db97ea78fe544dd8d7002ded25fd2eb`,
  `test-run-7ee40d3b48d145259a92702f155e185f`,
  `ap-runtime-52db725bb0224c089ab7f729777667e5`,
  `ipi-service-c65c5ad1b9054fc9b09178d9b2210d29`,
  `ipi-service-1ff709770f6949d3a1b412f3a7a81991` e
  `ipi-service-8f666d416dc340c5bbf5480e5f8f58a3`.
  Stress conclusivo 1/1 sulla stessa immagine a quattro vCPU, 15 minuti
  con shell e 15 dopo `exit`. Risposte bridge 155, sospensioni 11.470,
  delle quali 8.864 fuori finestra, zero sforamenti. Nel dispatcher
  62.400 cicli e 154.592 tick LAPIC complessivi. Pagine libere stabili
  a 16.033 prima e dopo il cleanup. Snapshot finale con IDT comune sulle
  quattro CPU, tre IPI AP privati, HPET e timer BSP e tre slot PS/2 liberi.
  Il runtime AP permanente e la preemption AP nell'immagine ordinaria
  restano nelle task successive. PC fisico
  non provato. Deroga utente limitata ai riferimenti necessari negli
  script PowerShell esistenti, migrazione completa della build separata.

- **Runtime AP permanente, 20 novembre 2026.** Task fatta.
  In `ordinary`, il BSP ammette e avvia ogni AP una sola volta. Admission,
  autorità e contesto del dispatcher persistono fra i job e dopo `exit`.
  Il motore continuo seleziona con IF=0 e attende con `STI; HLT` contigui.
  AP1 conserva i job finiti fino alla conferma di ritorno nel dispatcher,
  pubblicata con release dopo il rilascio del corrente locale. Il BSP
  acquisisce la generazione prima di raccogliere task e stack. Il mapping
  viene propagato prima di pubblicare READY. La sessione PS/2 rilascia
  il proprio binding di notifica senza revocare l'autorità AP.
  Kernel ordinario Meson SHA-256
  `1D12A5C3841B92E950D0CFFC6F53745061CADC7AD79E19DDF49C1CE655CFF652`,
  ELF di 507.904 byte. Loader SHA-256
  `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
  QEMU/OVMF TCG/q35, 25/25 boot conclusivi sulla stessa immagine, shell
  10/10, PS/2 assente 10/10, SMAP assente 3/3, una vCPU 1/1 e otto vCPU
  1/1. Nei casi shell con almeno due vCPU si eseguono tre job AP1 per boot,
  42 job complessivi con CPU 1 e somma 500.500 verificati. Il runner legge
  i contesti per CPU da 704 byte e confronta corrente, generazione,
  identità, completamenti e flag di ingresso permanente. Verifica IF=1,
  HLT=1 e RIP nel nuovo idle nello stesso snapshot di ciascun AP dopo
  il cleanup. Tre AP attivi a quattro vCPU, sette a otto vCPU. Senza PS/2
  gli AP restano nel primo idle e il contatore dei risvegli può essere zero.
  Pagine libere a quattro vCPU, 16.032 dopo la shell e 16.034 senza PS/2.
  Il contesto locale mantiene la dimensione precedente usando un campo
  riservato. L'ELF cresce di una pagina rispetto alla task IRQ.
  Cinque suite host superate, scheduler, x86-scheduler, ipi-service,
  kernel e watchdog. Il test del motore esegue 23.767 controlli con 64
  identità CPU sequenziali e core Rust reale protetto da lock. Per i 63 AP
  prova ingresso con coda vuota, arrivo di un task, completamento trattenuto
  e prosecuzione dell'attesa. CPU, stack e callback host sono simulati.
  ASan nelle suite host esistenti superato. Cppcheck su motore, storage
  locale, admission, servizio IRQ e ambito/job AP senza diagnostiche
  bloccanti. `plan_parity` 1/1 e runner Meson
  2/2, con 17 test unitari e sette di processo, superati.
  Il primo giro host non trovava Cppcheck. Kernel e watchdog erano passati,
  le altre tre suite sono state ripetute con `VOIDOS_CPPCHECK` impostato
  sulla copia già disponibile nel checkout. Il primo stress falliva perché
  avevo usato l'attesa di un marker per il campo numerico `SHELL_AP_SUM`,
  già presente e corretto nel log. Dopo l'autorizzazione dell'utente,
  `wait_field` conta i valori numerici usando la stessa gestione di timeout,
  processo e panic di `wait`. La regressione di processo verifica marker
  insufficiente, primo valore, secondo valore mancante e due valori presenti.
  Questa regressione usa un processo VM simulato.
  Stress conclusivo 1/1 sulla stessa immagine a quattro vCPU, 15 minuti
  di shell con 144 risposte bridge e 144 job AP1, poi 15 minuti dopo `exit`.
  Sospensioni BSP 17.538, delle quali 12.608 fuori finestra, zero sforamenti.
  Il dispatcher BSP ha completato 111.400 cicli, con 166.168 tick LAPIC
  complessivi e 16.032 pagine libere stabili. Lo snapshot finale legge tre
  dispatcher AP con 144 completamenti su AP1 e nessun completamento sugli
  altri due AP. Tutti e tre sono nel nuovo idle a IF=1, con almeno 292
  risvegli ciascuno. Corrente e generazione sono rilasciati. Tre IPI AP
  privati e IDT comune presenti, tre slot PS/2 azzerati e HPET/timer BSP
  ancora sottoscritti. La VM posseduta è stata chiusa dal runner dopo PASS.
  Le prove iniziali interrotte e gli smoke sono esclusi dai conteggi conclusivi.
  Evidenze in `kernel/build/ap-dispatch-*`,
  `test-run-753dee10d43c49fda57f1d0184df738b`,
  `test-run-fbca158110db4ce796dbf9f214db3260`,
  `test-run-8b1fd223006d432b9d35c23056f736e3` per le cinque suite finali,
  `meson-qemu-runner/meson-logs` e `ap-dispatch-cppcheck.log`.
  Restano timer e preemption AP ordinari, selezione senza affinità fissa,
  capacità e prove su PC fisico. I job AP1 continuano a eseguire a IF=0.

- **S1, task 4 — preemption AP ordinaria, 21 novembre 2026. Fatta.**
  L'immagine conclusiva `8F24D4FF` passa 25 boot della matrice, due boot
  mirati, otto suite host, trenta minuti di stress e la ripetizione
  mirata del runner. Il timer one-shot drena le richieste precedenti
  prima di riarmare. Restano task 5, capacità e prove su PC fisico.
  Il primo candidato usa timer periodico e policy per CPU. Il calcolo dei job AP parte
  a IF=1 e non contiene yield volontari. `ap <n>` usa AP1, mentre
  `ap <n> <cpu>` permette di verificare ogni AP ammesso. Il massimo è
  100.000.000 iterazioni. Un milione terminava fra due tick e aveva
  prodotto zero sospensioni, quindi quella prova iniziale era insufficiente.
  Kernel ordinario Meson SHA-256
  `724894CE3F4583A5FAEF43F7EEBF51EC73BFDFDC86B477D84BF413E6791F5938`,
  ELF di 507.904 byte. Loader SHA-256
  `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
  QEMU/OVMF TCG/q35, 25/25 boot, shell 10/10, PS/2 assente 10/10,
  SMAP assente 3/3, una vCPU 1/1 e otto vCPU 1/1. Sono completati 88 job,
  42 da mille e 46 da cento milioni di iterazioni. Tutte le somme sono
  corrette. I 46 record AP con lavoro lungo riportano da 22 a 54
  sospensioni e altrettante riprese. A otto vCPU lavorano tutti i sette AP.
  A quattro vCPU restano 16.027 pagine dopo la shell e 16.029 senza PS/2.
  I record di preemption riservano 20 KiB, cinque pagine in più della
  task precedente. Il timer e il token D1h restano attivi su ogni AP dopo
  cleanup e senza PS/2. Il runner legge policy, timer, contatori, ownership
  IRQ e dispatcher per CPU, incluso IF/HLT/RIP dallo stesso snapshot.
  Otto suite host superate, scheduler, x86-scheduler, ipi-service, kernel,
  watchdog, ps2, sync e percpu-clock. Runner Meson 2/2 e `plan_parity` 1/1.
  La prima estensione ad AP2 usava impropriamente la notifica console
  riservata ad AP1. Il completamento ora viene raccolto dal servizio BSP
  dopo la quiescenza AP, prima di pubblicare l'evento della shell.
  Lo stress ha poi trovato `LOCK_WAIT_FAILED`. I breakpoint prima del
  panic mostrano un proprietario vivo e ticket in coda. Le verifiche
  hardware del dispatcher ora precedono il lock globale. La regressione
  con due handoff reali riproduce il timeout aggregato nel sorgente
  precedente e passa rinnovando il budget quando avanza `Serving`.
  Deadline senza progresso e clock guasti continuano a fallire.
  Cppcheck ha trovato un incremento dentro `assert` nel nuovo test,
  corretto e verificato nella ripetizione host conclusiva.
  Una sessione parallela a otto vCPU ha ricevuto `ridge status`, con la
  prima lettera mancante, e non ha prodotto il marker atteso. In quel
  checkpoint la causa era indeterminata. La ripetizione isolata a otto
  vCPU è passata. Un'osservazione successiva con trace ha completato
  25 job AP a otto vCPU senza perdere caratteri. Le prove fallite,
  gli smoke e le sessioni interrotte sono esclusi dai conteggi conclusivi.
  Lo stress isolato ripetuto dopo il rinnovo degli strumenti supera
  15 minuti di shell e 15 dopo `exit`, con 130 risposte bridge, 130 job,
  zero sforamenti e 16.027 pagine libere stabili. Due successive VM
  parallele a otto vCPU hanno però riprodotto `LOCK_WAIT_FAILED`.
  Il solo rinnovo del budget e il campionamento ogni 64 poll non bastano.
  Quel candidato lasciava la chiusura aperta.
  Evidenze in `kernel/build/ap-preempt-progress-final-*`,
  `ap-preempt-progress-soak-resumed`, `ap-preempt-input-trace`,
  `ap-preempt-input-parallel`, `ap-preempt-lock-gdb*`,
  `test-run-7a42f3f500dd448c98abeea48a7aac15`,
  `test-run-38ae702c11374d78bff39b19f6be5e3a` e
  `sync-host-3c94127a5b184999bc1bc99ffad0c172` per il confronto rosso/verde.
  Il candidato successivo, SHA-256
  `7B5AB7C91E24FB39F5A45DB9E69F82D853A3FFF04031A2D704FC7662919FA38B`,
  usa un secondo senza progresso al posto dei 100 ms di bring-up.
  Conserva un milione di poll e campiona il clock ogni 64 iterazioni.
  Sul candidato sono passati altri 25 boot della stessa matrice e otto
  suite host, in `ap-preempt-runtime-final-*` e
  `test-run-5970269473504062aab9c8fd27941e2f`. Runner Meson 2/2.
  Due VM parallele a otto CPU completano ciascuna 15 minuti di shell e
  15 dopo `exit`, 128 risposte bridge e 128 job AP. Le pagine libere
  restano 15.815 in entrambe. Sospensioni BSP 32.355 e 32.861, zero
  sforamenti. Tutti i sette AP sospendono e riprendono i propri task.
  Il trace confronta eventi input, byte generati e letture effettive
  tramite `pckbd_kbd_read_data`. In ciascuna VM tutti i 3.717 tasti
  risultano decodificati e gli 11.151 byte generati corrispondono in
  ordine ai byte letti. Seguono cinque letture aggiuntive di cleanup.
  Evidenze in `ap-preempt-runtime-final-soak-eight` e
  `ap-preempt-runtime-final-soak-peer`, con `input-audit.json`.
  Il 21 novembre una nuova prova riproduce la perdita sul medesimo
  candidato `7B5AB7C9`, con otto CPU e carico host concorrente.
  `ap-input-current-load-eight` fallisce sul prefisso di `ap 100000000 4`,
  ricevuto come `ap 10000000 `. Il trace registra 342 byte prodotti e
  337 letti. La ricostruzione della FIFO da 16 byte coincide con tutte
  le letture e individua cinque scarti, con una raffica massima di
  19 byte fra letture. Il dump della memoria scrivibile, 986.880 byte,
  conserva decoder senza errori, zero resync e la riga incompleta.
  Durante il riempimento arrivano ripetuti D1h allo stesso RIP
  `audit_checkpoint+0x36`, prima dell'IRQ 41h. Il meccanismo indicato
  è starvation della tastiera durante il timer periodico. Quel run
  fornisce il riproduttore usato nella correzione successiva.
  La cattura negativa con perdita intenzionale del primo `b` ferma
  correttamente il runner e produce il dump. Questa prova valida
  l'acquisizione del guasto. I due test Meson del runner passano.
  Otto suite host passano in `test-run-e1fa07e123d7460ab1aea34749a77615`.
  Il candidato precedente `724894CE`, sotto lo stesso carico, fallisce
  a otto CPU per `LOCK_WAIT_FAILED`, mentre la prova a quattro CPU
  completa 37 round in cinque minuti e altri cinque dopo `exit`.
  Dopo l'approvazione dell'opzione A, il primo candidato one-shot
  `24132AC5` passa il boot ma fallisce uno stress parallelo per input
  incompleto. Il riarmo prima del context switch lascia scadere il
  quanto durante le verifiche di ripresa a IF=0. La FIFO scarta
  16 byte. Il riarmo viene spostato dopo quelle verifiche, prima di
  ripristinare IF, nei percorsi di avvio, yield ed evento. Il ritorno
  dall'IRQ e dalla sospensione per riga seriale riarmano in coda.
  Il candidato successivo, SHA-256
  `0B628CBBF72CAAE029A24452E4289F84DB42C4C3A3E02E048245F9868ACD3897`,
  passa 25 boot della matrice ordinaria, otto suite host e i due test
  Meson del runner, inclusi 20 casi unitari. La prova preliminare
  `ap-oneshot-tail-pilot` completa 42 job su sette AP in cinque minuti
  di shell e altri cinque dopo `exit`, con 15.814 pagine libere stabili.
  Lo stesso controllo FIFO del runner applicato ai trace reali fallisce
  sui due candidati difettosi, ai byte prodotti 302 e 111, e passa sul
  trace preliminare con 3.669 byte prodotti e letti, picco FIFO 8.
  Evidenze in `ap-oneshot-fifo-red-green.log`, `ap-oneshot-final-*`,
  `ap-oneshot-runner-final-tests.log` e
  `test-run-f09272d835354f50993446783be6e6fc`.
  I due stress conclusivi su `0B628CBB` falliscono prima della chiusura.
  `ap-oneshot-final-soak-eight` termina per `REMOTE_TASK_FAILED`. Il dump
  mostra due riprese AP successive all'ultimo progresso osservato dal
  BSP. `ap-oneshot-final-soak-peer` perde parte di `ap 100000000 6` e
  il controllo FIFO rileva lo scarto al byte prodotto 6.584. Il trace
  conserva D1h immediati dopo il ripristino di IF su task diversi.
  Il candidato `8F24D4FF` drena il vecchio IRR/ISR prima del riarmo
  e ricontrolla il progresso AP alla scadenza.
  Durante l'indagine emerge che il runner calcolava gli hash a fine
  campagna. Il JSON di `ap-oneshot-soak-eight` riporta `0B628CBB`,
  mentre la copia ESP provata ha hash `24132AC5`. Gli hash di tutte
  le 25 copie ESP della matrice successiva coincidono con `0B628CBB`.
  Il runner conserva ora gli hash iniziali e confronta le copie di
  ciascun boot. La regressione con VM simulata ricompila kernel e loader
  fra due boot e richiede il fallimento del secondo. Un test esistente
  del runner supera la scadenza del marker iniziale sotto carico. La
  ripetizione isolata passa 2/2 in `ap-oneshot-runner-hash-retest.log`.
  Il runner viene corretto anche nella lettura del timer AP. Il primo
  boot senza PS/2 su `8F24D4FF` fotografa `Open=1, Running=0` durante
  il drenaggio e viene respinto dal vecchio gate. Il gate aggiornato
  attende lo stato attivo entro la deadline. Passano dieci boot senza
  PS/2, dieci shell, tre senza SMAP, uno a CPU singola e il pilota a
  otto CPU sulla stessa immagine, 25/25 complessivi. Otto suite host
  passano in `test-run-d2c2c506a77a440685a7ceb303fd0e98`.
  Nei test con VM simulata l'attesa di avvio passa da 1,5 a 5 secondi.
  La finestra diagnostica resta 1,5 secondi e viene verificato che il
  marker iniziale preceda la scadenza del marker finale. I limiti di
  attesa del runner ordinario restano quelli esistenti. Le due suite
  Meson passano sotto carico in `ap-oneshot-drain-runner-tests.log`.
  Lo stress conclusivo `ap-oneshot-drain-soak-eight` su `8F24D4FF`
  passa quindici minuti di shell e quindici dopo `exit`. Esegue 116
  risposte bridge e 116 job distribuiti sui sette AP. Le pagine libere
  restano 15.814. Il BSP registra 161 sospensioni, 144 fuori finestra,
  zero sforamenti. Ogni AP registra sospensioni e riprese uguali,
  da 431 a 614. I 10.105 byte prodotti sono letti in ordine, picco
  FIFO 10. Il dispatcher completa 108.400 cicli e 112.853 tick BSP.
  La VM parallela completa 115 job e l'idle con memoria stabile, poi
  il vecchio runner respinge AP6 durante il drenaggio. Il suo report
  resta FAIL. Il controllo FIFO applicato alla traccia completa passa
  con 10.020 byte prodotti e letti, picco 10. L'errore di fotografia è
  coperto dal gate corretto e da `ap-oneshot-drain-runner-retest`,
  PASS con due minuti di shell e due di idle, 17 job su sette AP,
  1.494 byte prodotti e letti, picco 4 e memoria stabile a 15.814 pagine.
  Due ulteriori boot, `ap-oneshot-drain-tick-shell` a otto CPU e
  `ap-oneshot-drain-tick-no-ps2` a quattro, richiedono l'avanzamento
  effettivo del timer di ogni AP dopo il cleanup. Passano entrambi.
  Le suite Meson finali passano 2/2, con 20 casi unitari e otto di
  processo, in `ap-oneshot-drain-final-runner-tests.log`.
  La task 4 è fatta. La task 5, la capacità e le prove su PC fisico
  restano da eseguire.

- **S1, task 5 — selezione senza affinità fissa e migrazione, 22 novembre 2026. Fatta.**
  I job generici (`ap <n> auto`) possono essere eseguiti sul BSP oppure
  migrare tra le CPU ammesse. Prima della correzione al rendering console,
  otto suite host e dieci boot a quattro vCPU (80 job, 57 migrazioni reali)
  erano passati, ma lo stress a otto vCPU rilevava un overflow della FIFO
  PS/2 dopo 27 job (byte prodotto 2.472) mentre il BSP rendeva gruppi di
  celle a interrupt disabilitati.
  `x86_fb_live_group_pointer` in
  `arch/x86/kernel/drivers/video/framebuffer/live/group.c` ora esegue la
  sezione critica a interrupt spenti una cella alla volta, ripristinando IF
  e verificando l'ownership del framebuffer tra una cella e la successiva.
  Sull'immagine finale Meson `ordinary`, kernel SHA-256
  `53F4FFD226CC5CD3F83CB0798F4616420EFC2088FD3C89374AF7005FDA710AF3`,
  loader SHA-256
  `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`:
  test unitari `qemu_runner_unit` 21/21 PASS, smoke `--ordinary-shell` 1/1
  a 4 vCPU PASS, e stress completo `--ordinary-soak 30 --smp 8` 1/1 PASS
  (30 minuti di shell + 30 minuti di idle dopo `exit`): 235 job completati,
  67 job migrati, 261 migrazioni totali, `VisitedCpus=255` e
  `MigratingCpus=255` (tutte le otto CPU), 21.162 byte PS/2 generati e
  21.162 ricevuti con picco FIFO 3, 7.001 sospensioni BSP (6.988 fuori
  finestra), zero sforamenti, sospensioni e riprese uguali su ciascuno dei
  sette AP (423–1.197) e 15.813 pagine libere stabili.
  Evidenze in `kernel/build/s1-task5-smoke-fix` e
  `kernel/build/s1-task5-soak-30m-fix`.
  Limiti non provati dopo la correzione a `group.c`: le otto suite host
  PowerShell 7 non sono state ripetute su questo checkout (PowerShell 7 non
  presente nel PATH dell'ambiente) e hardware fisico non verificato.
  Restano capacità (rimozione limite 8 task) e prove su PC fisico.

## Build e runner

| Passo | Risultato storico |
| --- | --- |
| Pilota Meson | Su tre ripetizioni dell'editor di riga, mediana build+test 3,33 s PowerShell, 3,24 s Meson dopo setup e 2,17 s senza modifiche. Setup 7,82 s. Rebuild selettivo per C e Rust verificato. |
| M1, runner Rust | Parità sulla stessa immagine per `k5-copy` (30/30 per runner) e sei negativi (18/18 per runner). Coperti timeout, monitor, uscita e chiusura della VM posseduta. Dal registro, 7/7 campagne e 28 boot su immagini ricompilate. Windows, TCG/q35/OVMF. |
| M2, grafo Meson | Undici profili con stessi comandi, ordine e immagini byte per byte a parità di directory dopo `/Brepro`. Verificate invalidazione C, Rust e trampolino e nessuna ricompilazione senza modifiche. Quattro campagne Meson, 20 boot, più dieci boot singoli dei profili K6. |
| 12 novembre | A `efecb8b`, nove campagne ordinarie passano a Meson e 23 su 121 lo usano. Verifica documentale nel worktree con build separata: test host del loader completati, `plan_parity` 1/1, `ordinary` 1/1 con quattro vCPU e tre AP parcheggiati. |

Copertura, hardware fisico, build a un comando, CI e stress da trenta
minuti dell'immagine unica restano fuori dalle prove citate.

## Evidenze e limiti dei checkpoint

- Per il loader del 2 ottobre, i log erano nella build ignorata
  `firmware-test-9966cce985644e119bb26c4b8c259b44/`. Il preflight
  era verificato in QEMU, mentre ExitBootServices era provato con
  mock host. Il comportamento su alias di allocazione ambiguo
  trattiene la pagina per evitare di liberare la risorsa di un owner vivo.
- La validazione del 22 ottobre raccoglieva anche la correzione del
  rollover del timer, con 32 boot/sessioni finali e budget ripristinato.
  Per la console PS/2 riporta dieci boot shell, dieci ordinari e 612
  pressioni di stress. I dettagli e gli hash restano nei report dei
  moduli. [Rollover](consolidamento.md#validazione-dei-backend-x86),
  [verifica input](../../kernel/drivers/input/ps2/documentation/design.md).
- Nei checkpoint K3-A–H, i casi host di GS, clock, identità, risorse e
  lock non provavano l'avvio di un AP. Il test di riserva bassa K3-G
  passò 10/10 dedicati, 10/10 ordinari, negativo 1/1 e console VirtIO/VT-d
  1/1. K3-H passò 10/10 preparazioni, rollback 1/1, 10/10 ordinari e
  10/10 console, ancora senza avviare AP. [K3-G](#checkpoint-per-cpu).
- Il pilota del planner misurò tre coppie di build pulite con mediane
  di 27,50 s PowerShell e 34,48 s con `-UseRustPlan`. Su nove prove
  del solo piano, 315 ms e 23 ms rispettivamente, con planner Rust già
  compilato. Sono misure sull'host del checkpoint.
- Il confronto M1 dei parser usò 5012 log (12 reali, gli altri mutati)
  senza differenze. La VM estranea rimase intatta nelle prove di
  chiusura. M2 verificò parità di piano, PE/ELF e layout. Restano
  dipendenze locali negli script e profili diagnostici legacy.
  [Guida strumenti e prove](../../scripts/README.md).

Le note operative storiche su pulizia degli artefatti e campagne
manuali restano nella [guida agli script](../../scripts/README.md).
Il [registro del debito](../future-work.md) raccoglie i limiti video
e IOMMU ancora da valutare.

## Checkpoint per CPU

I report completi di questi passaggi sono al commit `efecb8b`, in
`arch/x86/kernel/cpu/percpu/documentation/`. I risultati si riferiscono
alle immagini dei checkpoint. Nei casi host, ASan riguarda il C host.
La chiusura K3 nel report di integrazione prova il gruppo nel boot
ordinario. Le campagne dedicate provano i casi di panic.

| Passo | Esito e perimetro |
| --- | --- |
| K3-A | Contesto GS BSP promosso e contesto iniziale azzerato. 50 esecuzioni C/ASan, 10/10 diagnostici, 10/10 ordinari, due negativi e una console VirtIO/VT-d. Solo BSP operativo. |
| K3-B | Clock ACPI PM collegato alle attese dei lock. 50/50 esecuzioni host, 10/10 diagnostici, 10/10 ordinari, due negativi e una console VirtIO/VT-d. Clock di contesa AP fuori prova. |
| K3-C | Registro di boot con indice logico distinto da APIC ID e UID ACPI. 20 esecuzioni C/ASan, 10/10 diagnostici, 10/10 ordinari, due negativi e una console VirtIO/VT-d. Registro verificato dal monitor, AP fermi. |
| K3-D | Arena con lease dati e tabelle, stack e descrittori privati. Host con 101 punti di guasto, 64 geometrie e sweep di 63 descrittori. 10/10 diagnostici, 10/10 ordinari, due fault attesi e una console. Risorse predisposte, AP fermi. |
| K3-E | Contesti e attese privati, commit che impedisce il rilascio delle lease. 70 esecuzioni host del clock, 10/10 diagnostici, 10/10 ordinari e una console. Quattro vCPU configurate nel diagnostico, solo BSP attivo. |
| K3-F | Panic NMI, record privati, ACK e seriale d'emergenza. 21/21 scenari diagnostici, 10/10 ordinari e una console VirtIO/VT-d. Un solo core OS attivo, quindi la prova non copriva il panic remoto. |
| K3-G | Prima lease sotto 1 MiB, pinned prima delle allocazioni generiche. 10/10 dedicati, negativo senza lease 1/1, 10/10 ordinari e console VirtIO/VT-d 1/1. Nessun ingresso AP in questo checkpoint. |
| K3-H | Root condivisa ancora inattiva, codice RO/X, dati RW/NX e CR3 del BSP invariato. 10/10 preparazioni, rollback 1/1, 10/10 ordinari e 10/10 console. 19.478 confronti ASan host, nessun AP avviato. |
| K3-I, NMI isolato | 13/13 esperimenti SeaBIOS su BSP, dieci avvii consecutivi e tre controlli. NMI trattenuto nel cambio di modo e consegnato dopo IRETQ. Zero boot del kernel e zero AP. |
| K3-I, sigillo | Commit irreversibile delle lease di gruppo con risorse trattenute su errore. 53 scenari host, 10/10 UEFI del sigillo, due arresti intenzionali e 10/10 ordinari. Il sigillo non avviava gli AP. |
| K3-I, ritorno | Piano IRETQ e preparazione del panic dopo il sigillo. 17 esperimenti CPU isolati, 10/10 UEFI dedicati, 10/10 regressioni panic e 10/10 ordinari. AP fermi. |
| K3-I, ammissione | Corretto il controllo di alias e overflow degli stack di emergenza. 17 prove CPU isolate, 10/10 UEFI joint-panic e 10/10 ordinari. L'ammissione nativa non era ancora raggiunta da INIT/SIPI. |
| K3-I, pubblicazione gruppo | Descrittore unico pubblicato release/acquire dopo sigillo e preparazione panic. Cinque casi Rust debug/release, 20 processi host con quattro lettori, 10/10 dedicati e 10/10 ordinari. Nessun AP avviato. |
| K3-I, contesto AP | Binding ID/GS, stato atomico e catena alta compilata. 120 processi host per contesti/clock, 50 prove BSP e 20 identità, 17 prove CPU del ponte, 10/10 dedicati e 10/10 ordinari. I/O e identità erano modellati in host, AP fermi. |
| K3-I, primo AP | 10/10 con il primo AP realmente ammesso e parcheggiato e negativo senza NMI 1/1. Due regressioni 10/10 su sigillo e ordinaria con AP fermi. Contesti privati e registri reali verificati sul primo AP. |
| K3-I, panic remoto | 10/10 arresti provocati dal BSP con ACK del primo AP e 10/10 avvii senza panic. Origine su AP e intero gruppo furono oggetto del checkpoint successivo. |
| K3-I, avvio sequenziale | 10/10 boot diagnostici con BSP e tre AP realmente avviati, negativo di readback 1/1 e 31 scenari host. Regressioni primo AP e ordinaria 10/10 per configurazione provata, con AP fermi nelle immagini ordinarie del checkpoint. |
| K3-I, panic del gruppo | 10/10 per origine BSP e 10/10 per origine AP3, più regressione sequenziale 10/10. Un reporter e tre ACK su quattro core realmente avviati. Campagne dedicate distinte dalla successiva integrazione ordinaria. |

I nomi delle campagne storiche restano sotto per consentire a
`scripts/clean.ps1` di riconoscerne gli artefatti locali.

<details>
<summary>Identificativi delle prove K3</summary>

- k3 `percpu-firmware-5cae2ecfd3e84971a40367b9c7968655`, `percpu-firmware-6bb6fdd5fdf54ee2bb73f8273acc28ba`, `pointer-interactive-check-96f08a2f5af544ae942972f666e74755`, `qemu-run-b369823cddbc495d99db668e04e4846b`, `qemu-run-ee393c5e7bc645c6ad87484593559392`, `test-run-4920c69028d4412d8ef27217a87ab36d`, `test-run-56e90f1e63644415b16b7518868892eb`, `test-run-f2af482d2ed94918915dd1fe48ae52ac`
- k3b `percpu-firmware-9d1541a2fccd49f58f165b91a4f7ba35`, `percpu-firmware-ae4f1cf1547142e9b083ba0bbd5ea2c8`, `pointer-interactive-check-7cf4efe402164fa89613752310f19079`, `qemu-run-bf80adb8eea04616a8505921bd0e2412`, `qemu-run-f982587b2b994ebfa892039c231213ef`, `test-run-3d290516085c45b7b10516cefb60cd62`, `test-run-8e55ce489f16413f94ff785cce0df95b`, `test-run-9f4399eef94e4d6d82fbcc03894841fd`, `test-run-9f7d17942d894f71bfa143641d121e71`, `test-run-aa7ad9f112df4a1a9a8514e0921d39d2`
- k3c `percpu-firmware-b271326e2f83458c931daa6c52b35496`, `percpu-firmware-b3606ad6fa18453f964adf6160fb68cd`, `pointer-interactive-check-b268b50fc63e4c7f841541b8746f339a`, `qemu-run-5da9d43e7be641a1924075ebdbf3b9f9`, `qemu-run-788115aa8c1a445387a2b79159247eaf`, `test-run-21056bb6a13641369e0f6f60529af04b`, `test-run-6f81d88ccd014e9594ffed57cd4b24a7`, `test-run-8acb158cfa21472ca25dd9c756f47e8a`, `test-run-e4be5524070440a6827474067513d35f`
- k3d `percpu-firmware-0bbb26b5efa94aee9a086cc7cbdb582e`, `percpu-firmware-4fa7a6fc7223469297cacd46e071486d`, `pointer-interactive-check-b22bd13be6244e9bb679bd3c895e3923`, `qemu-run-7e40b16f1ce640d89d284d4a09915a53`, `qemu-run-83657883265941d0aff729d02efd7a9b`, `test-run-1098e164755c449a893ee4a347e98463`, `test-run-27694c11fe0544f9a1ff078c44051f42`, `test-run-55fcfa069760496788f3c5d04e66dac8`, `test-run-71b4e32a8c80444c8b2707be652ca444`, `test-run-7395cb040fb044ff8a4107745ee45df5`, `test-run-794bafd50d99457e9da4b03998776848`, `test-run-8ff187fff96a4ebeb7def76a0f9b9edd`, `test-run-a987f9804f8144bea5e50675914cf9e8`, `test-run-a9ef4c7e9fd248d7a7cef2cb64a18455`, `test-run-c0d751e9cdba44ddbaa2e8fbb6aa3573`, `test-run-e8d0505a17084788ad8391e8416a4755`
- k3e `ap-resources-host-54a1777d0ff84a67bad3e6800ed83ec8`, `percpu-clock-host-d2c8df15b8184d9ea1348a354df08687`, `percpu-firmware-1810b9910c0240c5b109f2ebde1e2604`, `percpu-firmware-5351fb11d18f4c76a7d8d876a5ec7025`, `percpu-host-188e07c34ad3431798c2a3a82c7fca9a`, `pointer-interactive-check-2f171d50d2de43efae2e648ae96c152c`
- k3f `panic-matrix-3876d94f5bf54ff594d3a09308536744`, `percpu-firmware-f4ed3b1cf7f44f7089a3e7580acd5176`, `percpu-panic-host-9d511360c7824fc288da6282c9e8e864`, `pointer-interactive-check-319ab4f5238c44648619ad422445f644`, `test-run-25788ca0329942b09b65c7dd2f78e65a`, `test-run-5171bcd6b0cd4689a8cf3fcce42c720c`, `test-run-56d3628a156d4d2cb93fb167651fc019`, `test-run-a64a6d354e7c43a183f46ec3b613ff32`
- k3g `ap-low-firmware-b632e3d8c9b14bd6a767c42be80ff94a`, `ap-low-firmware-c620ac32b7d545a182196fb84080c97d`, `ap-low-host-ba199844c65d43f683641485dc3c80ec`, `percpu-firmware-c0747f36619b4c8eacd11df3b68d2f32`, `pointer-interactive-check-1a063c9e81a8403f9fdc8aff037748ee`, `qemu-run-1397d68cd14b4600b545d4cb5cce3937`, `qemu-run-7e1ae78ee92d44ee8b5be48cdd6b4957`, `qemu-run-c4e61cf582a148a387240cc1a5557580`, `qemu-run-d03e52362deb4d8da95887f46d98e485`, `qemu-run-decb077c6cfd4fd68687296f0ab3b300`, `qemu-run-f3fb01e8d2ee4158a34f019af21ff84a`, `test-run-32855490f8aa4c7aac50b9a9b2ae9d14`, `test-run-72cabc8c51d44406b4dc9cfe6f1864c6`, `test-run-7f051ffa44e34555bc1ddaa6855b7865`, `test-run-a225c3ca589945c7af81d1ce80ed1ee6`, `test-run-a784220388704bb2aa4814aa6317ee7c`
- k3h `ap-low-host-1c59c51ea40d463f8a5ddb78a8746a51`, `ap-transition-firmware-05d603b7816b4220961a1dd51f4a8c06`, `ap-transition-firmware-1ff414fc308743f5b211d62bbeba5542`, `ap-transition-firmware-4c5641e76c0a48d68efb1d610dd594f6`, `ap-transition-firmware-68154817706c4f5494eba3c96f60b7d4`, `ap-transition-firmware-87dd8dc3d89a4e06ab4de9309d38f505`, `ap-transition-firmware-c03c767045064c2a99a867833af33267`, `ap-transition-host-c9243349fad943d495fda73e963e58cf`, `bootstrap-host-934fafe68f544833a7e44c6178b973e4`, `bootstrap-host-a846797862a74bd6a638ecd3712027ca`, `percpu-firmware-be6d1b5beda9446d84d1e28993ef7233`, `percpu-firmware-f3ffaae930964505ac71e99fea3d80d6`, `physical-host-6b874efe75224d38a14dd3479e8ce271`, `pointer-interactive-check-0296a0ad972744f88b53e4aefe355a5f`, `pointer-interactive-check-6daec293f5fe45bb921b375d43c5c2e8`, `pointer-interactive-check-b4068248bbca431b82cce4f109a76183`, `qemu-run-dbd2702d77b748049e3e0d6e851aae01`, `transition-boot-profile-a8904285b0bd43ddb4421a6ff5af0d24`
- k3i-nmi `ap-nmi-experiment-50dc6e2c6fe24a4e8e97267be41ab4c9`, `ap-startup-host-ff23a85c1b0845f1a134cd7b2a2a306a`
- k3i-joint `ap-joint-firmware-58aa4b1a8ef14feda8bed11706a1112a`, `ap-joint-firmware-632fd2ba764342b2bd325f3bce78ed5b`, `ap-joint-firmware-b505e53b90c6435c894d4af2e3395260`, `percpu-firmware-5c9ba72e408d4040a5b6d3b8b69bad88`
- k3i-return `ap-joint-firmware-5f19e1cb4f454812b3bf2633513d0d7d`, `ap-return-cpu-0471914cd9c3426b97e079cbd93a59f3`, `panic-firmware-80a01520004d4361886e01a0cb2d7820`, `percpu-firmware-a67292590bf94ec0abb29bad6d2a1261`
- k3i-admission `ap-joint-firmware-7aaa7c9041cc44d4a539db033e9b243e`, `ap-return-cpu-5bfd8fbe1ffa466b910fd91bcc4405ea`, `percpu-firmware-1eddaa0f4f654101bf772bba7eebd920`
- k3i-group `ap-joint-firmware-e41fd2e71238439181107f59ca0442e4`, `percpu-firmware-7c30dd19d0264d7a99a74bdfeeab2f20`
- k3i-context `ap-joint-firmware-2e35e759615742c68c3538cbbe098945`, `ap-return-cpu-f95373a1ce274d59bf816c7faa8d922d`, `percpu-firmware-7dd4c453872246dc988d30c207d88e5f`
- k3i-first-ap `ap-joint-firmware-bf56e06a8a5b4fe991e03e9f87c00990`, `first-ap-firmware-2da8dd3ab8c047029b7418bfbc812013`, `first-ap-firmware-451593f7f1dc420b9b53d04916dfa679`, `first-ap-firmware-f3faeb25785247fd932f301e15501cb4`, `percpu-firmware-565c54e793954df8902ac706246a7beb`
- k3i-remote-panic `first-ap-firmware-471f31f863264bf9b1ef4c261e952f14`, `first-ap-firmware-8ee8990917714751bbee910d66d65546`, `first-ap-firmware-d178ff203d1f4aab92c0d44a6f63fda1`, `first-ap-firmware-ee0714b54c53436786da304991fc08c7`, `percpu-firmware-6504f73f792e4a46877a719345f8831c`, `test-run-6d25a0e85d984aae9b3b0ec1de7674d2`
- k3i-sequential `first-ap-firmware-3587d31d5a04452698b3b9ff6c829623`, `first-ap-firmware-395f31bd4ea342969b05cd91192b0d8a`, `first-ap-firmware-636b446023964cc893651d3b23db5d24`, `percpu-firmware-046b5ac0b6954bf3aa89221a1d8662d0`, `percpu-firmware-a193a7be97ee45e3b99da44a810ebbed`, `percpu-firmware-da0835a5e0944f1ab60f29ce99a401a3`, `sequence-failure-35b8e7292a6c4755945d8e24ee89ab45`
- k3i-group-panic `first-ap-firmware-ef39759b5dc144d79dfa99ef1658b953`, `group-panic-804e51d110b64ae591e86840e26916cd`, `group-panic-a45b3079d2864c6f8e6356c9409b8c7f`

</details>

## Validazione dei backend x86

Versioni complete dei report accorpati nelle cartelle dei moduli
x86, al commit `8bad38a`. Questi risultati appartengono alle
immagini dei rispettivi checkpoint.

| Area | Risultati e perimetro |
| --- | --- |
| K3-I, installazione inattiva | Host con 26 guasti di installazione, 22 di cancellazione e 8192 corruzioni di byte. QEMU 10/10 installazione, 10/10 cancellazione, 10/10 ordinari del checkpoint. AP fermi. |
| Chiusura K3 | QEMU/OVMF xAPIC. 10/10 ordinari a quattro CPU, 10/10 a una e 10/10 console GOP. Cinque profili di panic dedicati 10/10 ciascuno, con origini concorrenti, ACK omesso, DLAB e NMI ricorsivo. Soak separato di 300,953 s, 808 pacchetti e budget verificato. Sette suite host. |
| K4-A/B | PING ordinario 10/10 ciascuno su 1, 2, 4 e 8 CPU. Shootdown a quattro CPU, 10/10 per ordinario, rimappatura diagnostica, ACK omesso e console VirtIO/VT-d. Il negativo conserva il frame in lease, pinned, con i dati intatti. Tre shootdown nel teardown console per boot. |
| Staging ELF e paging | Staging R-014: cinque test Rust, C/ASan 100 ripetizioni, 10/10 boot QEMU sul BSP e quattro scenari di handoff, con fault attesi distinti. Inventario video 10/10 ordinari con metadati e 10/10 senza display, distinto dal mapping live. In K3-H, 19.478 confronti C/ASan e console finale 10/10 con timeout invariato. |
| Scheduler | B6 riporta 12 suite host e 10 boot ciascuno per ordinario, preservazione e sessione mouse/shell. Le mappe distinguono il probe diagnostico dallo switch ordinario. |
| HPET | Decoder C/ASan 100 ripetizioni, dieci fixture UEFI senza kernel. Discovery integrata in 11 boot, compreso il caso assente. Probe reale e comparatori verificati in dieci boot positivi. |
| Watchdog | R-001, 20 boot positivi tra finito e loop, 20 timeout attesi per IRQ LAPIC perso, due prove senza HPET. Fix ARM, 10/10 ordinari, 10/10 negativi e 20/20 stress video e senza video. Fix rollover, 32 boot/sessioni finali, compreso l'attraversamento reale del contatore. Il checkpoint recovery include 2.300 verifiche C/ASan e otto test Rust. |
| LAPIC | Il checkpoint K2 finale registra 10/10 con probe e 10/10 ordinari dopo il fix delle misure. La causa del vecchio panic resta indeterminata. |
| Framebuffer | Mapping live 10/10 ordinari, 10/10 senza display e 10/10 diagnostici con unbind, rebind e accounting. Le prove di pixel, disegno e font distinguono lettura della memoria video e verifica PPM nei profili diagnostici. |
| VirtIO/VT-d | Present asincrona 10/10 con host carico e 10/10 senza carico. Geometria 1280×800 10/10. Nel checkpoint H1-A, mediana su cinque boot di profilo, V2c 8.662 tick PM per flush contro 2.517 per GOP. Misure in QEMU TCG. |
| Routing e DMA | Routing 60 boot su sei serie, con root, nested e assenza tra i casi. Ottanta fixture guest sintetiche, una sola unità VT-d emulata. Il diagnostico separato `edu` verifica trasferimento, scrittura negata, pagina assente e revoca con QI. |
| PS/2 | Recovery B2 nella sessione PS/2 separata. Test host di decodifica e ordine degli eventi distinti dai boot e dalle prove di errore. [Report IRQ1→task](../../arch/x86/kernel/drivers/input/ps2/documentation/validation.md). |

Per ripetere i casi registrati usare
`./scripts/test/qemu.ps1 -Campaign <nome> -Fresh` dalla radice.
Il registro include `ap-runtime`, `ipi-service`,
`ipi-release-missing-ack`, `k3-soak`, `framebuffer-draw` e `v2c`.
Per HPET e paging i report storici riportano anche i comandi diretti
dei runner. Il watchdog riceve un ELF ordinario con `-Mode normal`
o un ELF diagnostico con `-Mode lost`.

### Identificativi delle prove x86

I nomi delle campagne sono conservati per il riconoscimento degli
artefatti storici da parte di `scripts/clean.ps1`.

<details>
<summary>Directory delle campagne storiche</summary>

```text
ap-assembler-5e05bbc296974dcfa08424b1a95c235c
ap-blob-cpu-312d6cad02c7493d908bb7b22ad9ff37
ap-blob-host-d00c56fd566541c89ca707f327d4f20c
ap-low-host-8bb26edfd9194b17b54199343a253430
ap-panic-stress-035cbe67c820486182ea2fb0b56884f9
ap-panic-stress-122bb1c97cbe4650a071959ae17a5fe4
ap-panic-stress-5d8312da66bc462cb1cdce7beca2c8dc
ap-panic-stress-6c16c142abe0455ba47e9da77f0f1fee
ap-panic-stress-849e150481204c8d91786a024974fd0e
ap-panic-stress-ccdc0ae534054cba8adb47ffa6b87289
ap-private-cpu-040ce4cc58f74511b066b310406d0013
ap-runtime-08b9981f6101447f929d086ef0bc37de
ap-runtime-0b4a05aaacea41659c1c1eede97f3957
ap-runtime-19afeafbd0394f9b93acab3131d2529d
ap-runtime-1c065124d2954d40a30bb99cfb8b8d27
ap-runtime-28e9fa38ab634e648131a25f27b9563a
ap-runtime-29bfa67c01b64d1f9a66b933af176db4
ap-runtime-2cbda29563254e7094c28e900c43a2fe
ap-runtime-5e3b418713cb412cba82d24bac61b55b
ap-runtime-60333af0b11b460689353b61760c0eff
ap-runtime-65af2dd68d744f2f9d5892704d76c4f8
ap-runtime-86bd7832e0424b778ce6840ce144669e
ap-runtime-8794f9bc64a24779a47a21fc0028f364
ap-runtime-c36b70ed949a4be7b30abacd1da321e7
ap-runtime-c49e762f6e4d4834812e0fbc2725d4ca
ap-runtime-d3151a2e131b41518e63acf3345d9a0b
ap-runtime-d52044afba554831814e10a493eee6a2
ap-runtime-d68e78b60c404b4cbb43f6d7c9209c23
ap-runtime-f44796cf425a4cc880d68ca48a0e2132
ap-runtime-fd3c0cd9433645ddb2206b0d61606dde
ap-runtime-ff4e8485ea6a40888acb5ed55aec116e
ap-runtime-host-46a6678105fd404988f16f32ed4dddb9
ap-startup-host-86a2c7cd725e42e99c0fad55e060fef0
ap-startup-host-ecee5552b904415b96591ccfd2a6ead8
ap-transition-firmware-1c455e37fc3a4223add02e82f3eefa6c
ap-transition-firmware-ad4dac22a3f64a97a2ace3751f8ca917
ap-transition-firmware-eea47a2fc182432987bf1d05a3e9e431
ap-transition-host-4302ba0b801e4191a31867bac6386cbd
ap-wake-host-3337a3b774104bf0b32b133f0df4289c
ecam-firmware-4b7b130ea4514fd78c092d8c18634a2a
ecam-firmware-d2455576721e4f74830351535327aae4
elf-validation-1beb54afb0b943008c550a1dda6002a2
elf-validation-90bba785a3b7492399beed27cf4dc64e
first-ap-firmware-4381b159da544e758150ff59edf2c893
framebuffer-drawing-previous-e956fefd5ee34d7e83760785db79f4cc
framebuffer-firmware-2896194a96cf49a99e6b57a09c306c4e
framebuffer-firmware-2cf78f29c1314636adade266a3b9cd7c
framebuffer-live-192ead97e3534f0f9c00d393e5840da7
framebuffer-live-29ce703f006640378d1c7259e0d792ec
framebuffer-live-32b6ca0d2e8042a49efaa016c4a8cf98
framebuffer-live-3bd11e6947444d11a8f828203cc644d4
framebuffer-live-6cbcb496240c493192c3c14bdc90798a
framebuffer-live-7a605f9a2b0b43529b7b605738e30cfa
framebuffer-live-8dca5f4c48564e409a041c2a73336144
framebuffer-live-9d1cf95cb0d2400b9473aec3f916645a
framebuffer-live-abb6b9c5c55b43f38869d472e008db8e
framebuffer-live-b52fc41b4ce44420affe7b72822fe989
framebuffer-live-b5d26ad255b5426d8cef014a9a8a5653
framebuffer-live-c03c81bc1d6e4bd5a65c4fd3abd88774
framebuffer-live-d51755e6ca0d472fa1d015c8266200e7
framebuffer-live-f4baedc6ecf34c47b0f3b89a7f34cb80
framebuffer-live-previous-8db96ba2de4f49fab7f6fdda24c90526
framebuffer-pixels-previous-fd1c0da2fb854f44b6990c0f50b27180
hpet-description-130b4e22f628419bb7ccfec1586b6dd2
hpet-description-1aefb7f79618452a9326645afd7b61d9
hpet-description-316c187527c24a36b75b340f86cb9def
hpet-description-3bef06369df74dce8aafd5762ede0d2a
hpet-description-a68a4c9e7cca4c36bd877e2a88a0f89f
hpet-description-beaebe055f4b4c32aa69d5756de017ed
hpet-kernel-3396b04c005740d4b37f574fe636dde8
hpet-kernel-34157dc9a0644075a3ae21e7c92ac3d6
hpet-kernel-3feaec0f5712459da236d6c529cdea2c
hpet-kernel-4e55ec307f6e4f3ba82d0b12c25506c1
hpet-kernel-5c780594266b45a58d640baa8ef8320d
hpet-kernel-d40d9eeddfda44619aa3304a5ca884fe
hpet-kernel-e539991fb5cd4a6cbc5a05eafa2c7495
iommu-discovery-firmware-9ef9b778be0e405f9e39a11f3a67bd99
ipi-service-0aea57d0a60e40bdb09800a1a3320bc1
ipi-service-0c6c75f30dfc4bd29842c1571c64d103
ipi-service-125d0d2673e4410b89344918a1d70e6b
ipi-service-1aa85b633daf4d4eb6b28d46ac12e89c
ipi-service-260d1ddc928748c38c9c16f580e696d8
ipi-service-4701a9223941449899e2569d13039312
ipi-service-480462505f0b4792abc946e016c891d6
ipi-service-4bd8ad49de5e478bbf7b668af4ae7c86
ipi-service-5a753968147048d3a2c420439a318247
ipi-service-5f2b7ba867114dd78ae331a99783d04f
ipi-service-7819008bec0a4cd0bc337b12ebccd40c
ipi-service-88fb0f6bc2f343f19f1dd77b8f300f06
ipi-service-8b44d81387e84b34af0f233b266bffb5
ipi-service-95a618ad414a49b1ba85bae0f660097e
ipi-service-996483a1576f4c30b8bf6ae730956347
ipi-service-bb279c97a26a40a0b3c7a015a4ebee15
ipi-service-dc52577db8aa430bae3854d7f26b6a57
ipi-service-dd2035d54354456c8d0254055e35ce4e
ipi-service-e1eb5debac4c4ff1973fe37541813f85
ipi-service-e3a567f071d44d958403e9e0be6cd44c
ipi-service-e6e32cfa0e9f4048a1f2a3c33c490422
k4b-vgpu-resources-7fbb1ed9de3949b383b347556a02f7f2
k4b-vtd-registers-e6a3043142ef4c3b87a3cf82774dcbd2
k4b-vtd-startup-sample-c10ace20f57e4a92adbda9c3a047d1cb
kernel-test-673c0771f3544f2088112efeb9f10203
lapic-asan-7636953c24fa433da01729c63eada60d
lapic-asan-d4205d5ed81a4c48896f32e37bfbb8f2
lapic-asan-e8e804e8518d4d879582aeb7f2bda0d6
lapic-asan-ec23ae7e439d468ea415c3b3f7fa6add
lapic-calibration-2abb4b77f07d47e5ba56204730f3a12a
lapic-calibration-firmware-70d92ae1709048a28c74a9e66a266340
loop-firmware-ad829196cdaa419381a5c7719d17e1b4
percpu-firmware-14e760e444be492a96c551a2239e30cd
percpu-firmware-5b5514158a724d1cb66edb45184dcd0a
percpu-firmware-f2d17be598924d9ea0dec1a4f3b337c7
pointer-interactive-check-3487753778c7468e991226dd204828c2
pointer-interactive-check-3c9a28203a89410c956e5f05dbb78cbe
pointer-interactive-check-4c9eb54eddb3432fbaa14b5a73cf7092
pointer-interactive-check-5f30e611e0104454a545eb67cb4318d9
pointer-interactive-check-694bbfb4ad764c248ee8ad5606391652
pointer-interactive-check-75d6d633c3614cb2a1a1759d7e9a2f58
pointer-interactive-check-8060f506f06540cf8ee50df52a3e290e
pointer-interactive-check-8bc6df44815740859ed44079593ac28c
pointer-interactive-check-9df8d0ddcfc946978009d30c89cb3607
pointer-interactive-check-a77e2c6c015943298a18d4d537b75631
pointer-interactive-check-ae9adeaa4dd84b96947671580ad045e3
pointer-interactive-check-af96829a495e4c399039e1db4adbc1cc
pointer-interactive-check-b08ff43e062e4e148b153b04b0608de6
pointer-interactive-check-d23896c81544492ea43a30bf8c5b6565
pointer-interactive-check-deccf73f98664f2896cf5512b0add9d2
pointer-interactive-check-e63db3a956ca40a7b378f89f47f1e67f
pointer-interactive-check-efe51c2da094418ba066d06fbed1fcf3
ps2-decoder-2c6352880d45484b807cba144a3dd263
ps2-input-firmware-0b510fe6d4f948e9adbe4195a8f02f9d
ps2-input-firmware-16e0c900a3344407b8872ef700740e0e
ps2-input-firmware-1d6c149a82c947f188150759ba1b9435
ps2-input-firmware-2e8704c72a9a4de9b5023437a98b3eca
ps2-input-firmware-3b321d3ddd48478b9b269fba12d0e3af
ps2-input-firmware-4bf7d3be7c7b4b8b8fe73340263bc221
ps2-input-firmware-5d64d3b2c1cb49f9810f3adf3ebd99dd
ps2-input-firmware-64cf23a7c12d4587ab6225fc293e8159
ps2-input-firmware-7ad26c2fe8e04790b13366debd6b6d50
ps2-input-firmware-85fce2062fdb4ef5a490c3100cd6e7e3
ps2-input-firmware-8732dad7475b41c4a12ffd612c06bb1d
ps2-input-firmware-88a4a950a6094dbca455886d7f153137
ps2-input-firmware-94d1eafcff4b4c029422718b413879bf
ps2-input-firmware-a2b5b8658d5246038052edb68a2a384e
ps2-input-firmware-d6ec35b7215b4e639c5303e27a0541a4
ps2-input-firmware-d7e69479bbba496eb752d65d7e2770fd
ps2-input-firmware-ef12b928799c45eba8bb573aa06d060b
ps2-input-firmware-f367093b81d84028b3e8593cadb04e5b
qemu-run-ac9ec387641c411db12be32354a2e7ad
scheduler-context-650e9ab43cfc45c1ac9b8b97ecd0c18e
scheduler-context-851bcf5263a942bb841dd2828017140e
scheduler-context-c63b049db80242fe8556f93ad5c6a6d7
scheduler-context-cb8fdad2f96d48cabe056ea25662c976
scheduler-firmware-192dc41896e14c558f1caaa26c6094eb
scheduler-firmware-1e5af66443ec4b4485fecc811bbe0988
scheduler-firmware-3988eb4148e549678b83c03210f9496f
scheduler-firmware-542d9df75d40483baa4f1389700d1679
scheduler-firmware-c6776a75efb04428b7fd5712e8b11949
scheduler-firmware-d27f2a31466b44e38bd0de4023b961ce
scheduler-host-cea26f0ab8bd4dd5ab015a8fffa05f1f
sync-firmware-08b09deaae29459ca2ec81beba1a9b51
sync-firmware-0e64e7ef8e2f4a2c846bf2e7610da199
sync-firmware-1527d6936e0b4e12b3707029de0ad48b
sync-firmware-43a26d629ad44d1498c1e4442db879d7
sync-firmware-a83ba4576c6645bc9ec4ddbe181c8dc6
sync-firmware-c4778d3492f74fc882c50c9ddf398efd
test-run-05cf54e0a9eb4720b535d5f6070293b6
test-run-07d2b92e1edd469d820c71b95d4a1bc1
test-run-13c38bbc85814c958b2c53bd266f2c6b
test-run-19bc7aa7423f4830b5054221b5e0bb44
test-run-2d645386f6b048df8c2c8b42652cc417
test-run-34ba894ba9a54013a1805f0e7f360c4f
test-run-3af6ec2f0497450ea0ce7626a0333633
test-run-3d1167f4a4cf4d918f5307f003d37c06
test-run-3d5966fb1635490b9f0e2cfa4a7db584
test-run-52ac4958ff014c0f90a669f1ea5690f5
test-run-5c616c8e82424dcbae68fe6284289db4
test-run-679c9554be4e488e988ede1300f3a6b6
test-run-6d37d33b37e6424280cebb07bcaf0cf7
test-run-770fab1d24bc4a30b76dbeda8950b956
test-run-7a4aaa40cdc64565bfeb6a5b9d7ad92c
test-run-8e5bfe52361a44c282bf7328c1ba4186
test-run-90a1b98d9ae34b6081648576b613dca2
test-run-9aa863b2c7fb4e85914b1ec00d5c47b6
test-run-a84b1a5dc5844cf69ce02cb4db6627ae
test-run-ad94eaa10167405d913067f88b9aa496
test-run-b4232534ac684bb0be0dc43943236230
test-run-b6a9c6054eac4c528f8039a8894ee1d8
test-run-c2c44248543047198a0fdafd7fc6b329
test-run-d2db16b216c4417eb84a0c32beb71812
test-run-d32032b258934e528583b73e24ee25f1
test-run-d6f50802c94d412588b6a7b9d47343de
test-run-e63338558d50401bb4c7a9ae9da1c919
test-run-eaf4a4eb95554eb1adb08676f4428abb
test-run-edc65259aa06492aa2f01450c7cc71d7
test-run-f30bcdadb732416b873b5af320c3bc36
test-run-f73f2190f0834d3dbf11e4edba2ff90a
test-run-f90480fe646d4154a5bdcb8bbfe5e7f2
watchdog-absent-739dd922ff594bccbb20a4528f9115f8
watchdog-arm-install-4c42929ed04342a2aa3c4a3674e0262b
watchdog-arm-stress-614ba8c00edc49fda181d1f8e817b507
watchdog-arm-stress-aa31b46a923f468db9fcc8db337ea5bd
watchdog-compatibility-82e43a8f2650407cbbcd517e0f87200c
watchdog-compatibility-9e55c64dce3c49a986098d29c052382a
watchdog-compatibility-cd5fac9434fa47b99ed109662189ee8a
watchdog-host-753b8da4bc0147b68ea7ac3010a4e8e3
watchdog-host-76f7b1eaced94d88809eeb5e9e895ef5
watchdog-host-a5be41cd6acf4ca1bd8fe186b5434597
watchdog-host-c0cfa2e873c94fe99aa39f126acee48d
watchdog-host-c261c47426d542a0aeaedb576054afb2
watchdog-host-d4c3110684874e5bb51c4b335e04922c
watchdog-host-f92e14c8799a4e11b4609bc1afe705ab
watchdog-lost-b7781d457a754173bff8675a5e46e1d5
watchdog-lost-bb8810c1de4443a3a7ec377bb72a2255
watchdog-lost-c741e4feaa0a4f93abc4236f7f78df6b
watchdog-lost-cb5496dabc0e4bc8b471b90d5ea0e777
watchdog-lost-d8a31cdf6d70415b8284af2d702fd6c0
watchdog-matrix-0768c029034349888a4bfc5ffbf05d25
watchdog-matrix-1cb6b1359eb0470983fc82c951e05915
watchdog-matrix-1cc292a7d3b345b5957ed719f90d6dcc
watchdog-matrix-40673f41ec5240a3b47513c8a31cad25
watchdog-normal-3adcd1c7389c4c2a9d099b932360838c
watchdog-normal-49406f403d3440c89e637408614272c5
watchdog-normal-7d4dcae4672d4bef9294bd91489d4750
```

</details>
