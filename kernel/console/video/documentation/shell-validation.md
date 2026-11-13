# Console operativa della shell: validazione

## K4-C5: trasporto IPI reale AP1 -> BSP, 6 novembre 2026

Trasporto, handler e consumo dell'evento nel dispatcher sono verificati nel
diagnostico dedicato. Il BSP apre finestre IRQ bounded, senza HLT e senza
una task worker in WAIT. C5 non e chiusa: restano il risveglio del worker,
il binding comune BSP/AP e l'integrazione nella console ordinaria.

| Gate | Risultato | Evidenza in `kernel/build/` |
| --- | --- | --- |
| Rust/FFI | Notify 7/7 debug e release; channel 2/2; Clippy no_std e FFI, warning come errori | `c5-ipi-host-reviewed.log` |
| C/Rust ASan | sei esecuzioni FFI e quindici esecuzioni backend nativo: CPU/IF/task, destinazione BSP ID 37 nel modello, coalescing, ESR, clock, post/EOI falliti, quiescenza e close | `c5-ipi-host-reviewed/` |
| Regressione gateway | produttori concorrenti, 8.000 richieste per profilo, ordine/FULL e adapter input | `c5-ipi-host-reviewed.log` |
| Analisi statica | tre configurazioni, nessun error/warning/performance/portability bloccante | `c5-ipi-host-reviewed/notify-static-*/cppcheck.xml` |
| IPI positivo | 10/10 consecutivi, quattro CPU reali, 2.000 WRITE per boot, FULL e FIFO, 32 pagine stack recuperate | `ap-console-e6518738fa954607b6739e7eb266bc22/results.json` |
| Ack mancante | 10/10 arresti attesi; observed=0, close negata, due stack pinned e guardie conservati | `ap-console-cef4b9d9257842deba6e87f99a859db2/results.json` |
| Invio in corso | 10/10 arresti attesi; un riferimento residuo, close negata, nessuno stack raccolto | `ap-console-7ce51e7713a64aaea602b3d6a8d8167f/results.json` |
| Console ordinaria | 3/3 consecutivi, quattro CPU, pixel, tastiera/mouse, shell, Esc e cleanup | `pointer-interactive-check-b3d9d9448d6149b3b51d65e302136c4a/` |
| Audit simboli | backend/notifier presenti nel diagnostico e assenti nell'immagine ordinaria | `c5-ipi-symbols.log` |
| Reader/layout | quattro test reader; 508 sorgenti test centralizzati, massimo 490 righe, nessuna violazione | `c5-ipi-host-reviewed.log`, `test-layout/layout.json` |

Livello adattivo complesso: dieci boot positivi consecutivi, due campagne
negative distinte di dieci boot ciascuna e tre regressioni console.
QEMU q35/TCG, OVMF ed ESP privata per VM. Ogni reader verifica marker,
stato reale delle quattro CPU, ownership, lease/pin e budget. Il positivo
restituisce il budget 16.208 pagine; i negativi conservano le 32 pagine
dei due stack, con budget 16.176 e raccolta zero. Il panic SMP e atteso
soltanto nei negativi, non viene contato come boot operativo riuscito.

Nel positivo: 2.000 sequenze pubblicate e osservate; 653-689 scritture ICR
e altrettante consegne/EOI per boot. Il protocollo permette anche ulteriori
raggruppamenti APIC: il reader richiede consegne <= invii, non uguaglianza
con il numero di WRITE. Questi numeri non sono benchmark di latenza.

Immagini ELF SHA-256:

- Positivo: `CE9EACF9D6E5AD5372C5CA3A3BF6E4DB79D04477AAE5FC003690EFCA0EB8B613`.
- No ack: `83E7DB9FF8AAB571BAD218B486C506A8DFF7ED826E806865CA67B718A4564122`.
- In flight: `82163158D05D745CE2926D6C4ED324AEE9E981A21E73690AC7294C44EC0F696B`.
- Console: `2D74E9BAC129F9C4DC808F28FBD1D759793A87B203A0D999E38A06739E0F8B25`.
- Loader: `37F4DE1FCDB792D2684D1F1C8B37F7EFD8DB75F0CA47AE3B7887088BAB119854`.

ASan strumenta C host, ICR e gateway; registri, registry e dispatcher del
nuovo test backend sono modelli. Il file MMIO privilegiato e verificato
solo dal guest. Rust non e strumentato da ASan; non vi sono nuove
allocazioni. Il ripristino della RAM fisica e verificato separatamente
in QEMU. Valgrind non disponibile su Windows; coverage linee/branch N/D.
Nessuna prova su hardware fisico, nessuna garanzia di latenza o WAIT/HLT.

Review finale: nessun accesso alla griglia dagli AP/IRQ, lock della coda
rilasciato prima dell'invio, callback senza borrow dell'Owner, rilascio
solo dopo sigillo/quiescenza e revoca dei binding. I controlli di stile
non rilevano trailing whitespace o tab; sorgenti modificati massimo
393 righe, cartelle funzionali entro otto file. Il prossimo gate e una
task BSP realmente sospesa e risvegliata, con attesa HLT protetta dal
watchdog; non basta riutilizzare questo supervisore a polling.

## Checkpoint: base Rust e quarto slot IRQ, 6 novembre 2026

Scelta IPI AP1 -> BSP acquisita. Nucleo della notifica verificato host e
registro BSP esteso a quattro slot. Trasporto ICR, handler e risveglio del
worker restano da collegare: nessun boot sotto dimostra gia un IPI 0x43.

| Controllo | Risultato | Evidenza in `kernel/build/` |
| --- | --- | --- |
| Rust Notify | 7/7 debug e 7/7 release, overflow checks e Clippy; 500 gare seal/invio e 4.000 pubblicazioni concorrenti per profilo | `c5-wake-notify-final.log`, `c5-wake-requests/` |
| Gateway C/Rust e adattatori esistenti | ASan C, quattro produttori e 8.000 richieste per profilo; handles stabili, due profili nativi e cppcheck | `c5-wake-requests-host.log` |
| Suite host kernel | warning come errori, registro a quattro slot, validatori IRQ e EOI 0x43 | `c5-wake-kernel-host.log` |
| Registro IRQ ASan | 100 ripetizioni di otto gruppi; quarto slot, ID duplicati, token vecchi, distacco e consegna non posseduta | `c5-wake-registry-asan.log` |
| Watchdog host | ASan C, 100 ripetizioni, pendente 0x43 owned accettato e unowned rifiutato; rollback e adattatore MMIO | `c5-wake-watchdog-host.log` |
| Analisi statica adattatori IRQ/watchdog | nessun error/warning/performance/portability bloccante | `c5-wake-static/cppcheck.xml` |
| Regressione AP console | 10/10 consecutivi, quattro CPU reali, 2.000 WRITE per boot, FIFO, FULL e 32 pagine stack recuperate | `ap-console-fe4be9b040b7426998c384764704726b/` |
| Regressione console ordinaria | 3/3 consecutivi, quattro CPU, tastiera/mouse, pixel, shell e cleanup | `pointer-interactive-check-8602a6bfe8064a1a8abe20daf6c35e3b/` |

Livello adattivo complesso: dieci boot positivi del profilo AP, piu tre
regressioni video. QEMU q35/TCG/OVMF, ESP privata per VM. ELF AP:
`49C4D38BC589E7F57EE398C68119D2AE367E9E59CF443F61F6BF5104D998BAA8`;
ELF console `5FC91822714552CF62394D91FA4C0D655F06BDCC1E6B6D450CE637E2B72021C1`.
Loader `37F4DE1FCDB792D2684D1F1C8B37F7EFD8DB75F0CA47AE3B7887088BAB119854`.

Audit simboli: `c5-wake-symbols.log`, diagnostico presente nella sua immagine
e assente in quella ordinaria. Layout centralizzato: 505 sorgenti test,
massimo 490 righe, zero violazioni. Diff finale senza trailing whitespace;
sorgenti modificati massimo 349 righe.

ASan strumenta il C host, non Rust o la RAM fisica guest. I test Rust usano
thread host e ruoli CPU simulati, non l'IPI hardware. Notify non alloca;
accounting guest e ripristino degli stack appartengono al diagnostico
preesistente. Valgrind non disponibile su questo host; coverage N/D.
A fine checkpoint restavano necessarie prove QEMU di WAIT/HLT, IRQ/EOI reali e rifiuto del
cleanup con ack o invio mancanti prima di dichiarare completo il risveglio.

## K4-C5: diagnostico AP1 -> coda -> BSP, 6 novembre 2026

Due task su AP1 inviano 1.000 WRITE ciascuno al consumer BSP in RAM.
Il drain inizia solo dopo un FULL osservato da entrambi. Il consumer
verifica identita e sequenza per produttore; solo dopo fine degli invii,
quiescenza AP, drain e chiusura vengono raccolti gli stack.
Nessun disegno o risveglio della console ordinaria viene effettuato dagli AP.

Evidenze finali, q35/TCG/OVMF con quattro CPU reali inizializzate:

| Profilo | Esito | Evidenza in `kernel/build/` |
| --- | --- | --- |
| AP console positivo | 10/10 consecutivi, 20.000 WRITE, 32 pagine stack recuperate per boot | `ap-console-43f9710ec3ea4fbfa4c75108103a6142/` |
| Conferma mancante | 10/10 arresti attesi, 32 pagine ancora mappate e pinned per boot | `ap-console-13c1d6a5124f47089e6812c1082fa330/` |
| Console ordinaria | 3/3 consecutivi, input, pixel, shell, Esc e cleanup | `pointer-interactive-check-8a7375bd47b24018aeec27bc2c770025/` |

SHA-256 ELF positivo:
`3139622ADA0C189F67A73B525ADD8105BAC5F8A196D3B7875E8B006C4CAE8D7A`;
negativo `621756DD75D5E209693973775554DD4C305FC34BBF935161451EFB76C9D854B2`;
regressione `54B2DDAADC6231D0E790BCC24082C1AEEA36A5F417A1EDBBDACD6C4C4E82DBBD`.
Loader invariato: `37F4DE1FCDB792D2684D1F1C8B37F7EFD8DB75F0CA47AE3B7887088BAB119854`.
I due smoke 1/1 precedono le campagne e non entrano nel conteggio finale.

Il lettore QEMU riusa verifiche CPU/GDT/GS/root/IRQ del batch, controlla
record, FIFO per produttore, FULL, budget fisico e PTE delle guardie.
Nel negativo entrambi i task sono FINISHED e AP1 e quiescente: manca
solo una conferma logica. La chiamata a cleanup viene negata prima di
COLLECT. Il lettore controlla tutte le 32 PTE dati, le quattro guardie,
le due lease da 16 pagine e i pin; i panic sono attesi e coordinati.
Non e una prova di AP bloccato o recovery. Gli IPI vengono conteggiati
dai contatori guest, non da una costante attribuita al nuovo caso.

Suite host console completa: `kernel/build/c5-ap-console-host-final.log`.
Verifica finale mirata: `kernel/build/c5-ap-console-proof-final.log` e
relativa cartella, con Rust debug/release e Clippy, ABI C/Rust, ASan C,
quattro produttori host e controlli cppcheck del diagnostico.
La macchina a stati safe Rust verifica identita, token stantii, duplicati,
chiusura incompleta, riuso negato e completamenti concorrenti in 200 round
per configurazione. Il test C esercita anche il vero export FFI del canale.
Il lettore delle prove rifiuta record alterati e retention incoerente.

L'audit iniziale non vedeva i membri Rust `archivio:*.rcgu.o`. Il parser
corretto passa i test positivi/negativi, inclusi simboli Rust vietati,
simboli richiesti assenti e oggetti test. Audit reale: 3.657 simboli del
profilo ordinario, sette simboli vietati assenti e due richiesti presenti;
3.221 del diagnostico, quattro entry AP richieste presenti. Nessun oggetto
test nel primo; oggetti diagnostici esplicitamente ammessi nel secondo.

Limiti: C/ASM riusano il backend verificato, senza nuovi accessi hardware.
Rustc locale 1.98.1. ASan copre il C host, non il guest o Rust; Valgrind
non eseguito, coverage linee/branch N/D. Accounting fisico guest distinto.
Le attese del diagnostico sono bounded e usano yield, non WAIT/HLT SMP.
La console ordinaria resta BSP; restano risvegli cross-CPU, binding comune
dei task, driver PS/2 e ulteriori moduli C5. Nessuna prova su hardware reale.

## Checkpoint precedente: worker BSP collegato

Input e shell accodano le operazioni; solo il worker applica testo e riga
al servizio C. Implementati attesa per spazio con generazioni, invii copiati,
massimo otto applicazioni per turno e drain/close prima del teardown.
La shell pubblica il comando solo dopo aver accodato il SUBMIT video.

Verifiche sulla modifica finale:

- Suite host console completa a PASS in `kernel/build/c5-worker-suite-final.log`:
  Rust debug/release, Clippy, MSVC W4/WX con ASan C, quattro report cppcheck
  e verifica separata del gateway con lock reale e quattro produttori.
- Test del consumer reale C: EDIT applicato solo all'estrazione, WRITE
  conserva la riga, SUBMIT la consegna; errore applicativo rende FAILED
  irreversibile. Il vecchio test della sessione usa mock del nuovo confine;
  non viene dichiarato prova del protocollo della coda.
- Build nativa con nodefaultlib e audit simboli passati. Le prime prove di
  link hanno individuato funzioni core Rust mancanti e dipendenze Windows:
  risolte usando staticlib con LTO, senza stub o librerie Windows aggiunte.
- QEMU GOP/mouse: 10/10 boot consecutivi, q35/TCG/OVMF, quattro CPU
  inizializzate e tre AP parcheggiati. Pixel, movimento, tastiera, shell,
  Esc, raccolta degli stack e budget fisico prima/dopo verificati.
  Il runner richiede anche `KERNEL_CONSOLE_REQUESTS_CLEAN`.
- Tastiera senza mouse: 10/10 boot consecutivi, testo italiano, shell,
  IRQ, idle e cleanup; marker della nuova coda verificato nei dieci log.
  Fallback seriale senza video: 3/3 boot sulla stessa immagine tastiera.
- Adattatore nativo compilato nei due profili tastiera/mouse, C con ASan:
  FULL, segnale prima del WAIT, risveglio senza spazio, limite dei tentativi,
  massimo otto applicazioni e stop solo dopo la fine di entrambi i producer.
  Usa il vero `console/requests.c`, con coda/scheduler mock; il test separato
  del gateway usa invece lock e nucleo Rust reali.

Immagine GOP SHA-256:
`2C3F0DC6B6F8442099BF57926244EF0C3664D9191387845E74CC4E5C19AD683A`.
Loader: `37F4DE1FCDB792D2684D1F1C8B37F7EFD8DB75F0CA47AE3B7887088BAB119854`.
Campagna finale: `kernel/build/pointer-interactive-check-515792a252fb4e5190f8a1bc4dac7c1a/`;
log `kernel/build/c5-worker-qemu-final.log`. Smoke 1/1 separato in
`pointer-interactive-check-71d62dc614fa45d6968ef4c4159e4518/`.

Immagine tastiera SHA-256:
`0836BC2AB151841A255B4AA07EA8DC8CBF4F5CF206DFB24BCAE3A7F9274FD7E5`.
Campagna `kernel/build/qemu-run-ee85fab4707b4d629da90be313e6bbc5/`,
prove `kernel/build/ps2-input-firmware-57fedd6115644d12a18d6c0a5764a7f0/`;
fallback `kernel/build/ps2-input-firmware-4503eeacf30147cab3f0b9819ecfa7d9/`.
Test protocollo e analisi statica finali: `kernel/build/c5-worker-protocol-final-check/`
e relativo `.log`. Il layout centralizzato e il diff passano i controlli;
il sorgente modificato piu lungo resta `runtime.c`, 455 righe.

I produttori nativi restano sul BSP: manca il collegamento dei task AP agli invii e ai risvegli
della sessione. Mouse e altri driver non diventano SMP-safe con questo
blocco; C5 resta aperta. Nessuna prova su hardware reale o VirtIO in questo
blocco. ASan copre il C host, non Rust/guest; Valgrind non eseguito e
coverage linee/branch N/D. Accounting fisico guest distinto dai test host.

## K4-C5: checkpoint precedente del gateway C/Rust

Implementato il gateway per sessione in `requests/queue.c` e
`rust/ffi/requests.rs`. Usa il vero ticket lock K2 con contesto distinto
per thread host. La CPU nativa, gli IRQ e il clock hardware non sono
esercitati: i relativi adattatori restano mock.

Verifiche completate:

- Suite console completa a PASS, incluso il gateway, in
  `kernel/build/c5-console-gateway-final.log`.
- Gateway Rust `no_std` debug/release, Clippy senza warning; C MSVC W4/WX
  con ASan. In ciascuna configurazione quattro produttori consegnano
  8.000 richieste al consumer: nessun duplicato, ordine per produttore,
  applicazione al servizio C fuori dal lock, drain e close completati.
- Negativi C: oggetto copiato, reinit, output invariato su errore,
  sovrapposizioni con owner/contesto, puntatori disallineati e overflow
  numerici respinti prima della dereferenziazione; capacita, UTF-8,
  piena/vuota, conferma invalida, chiusura prematura e FAILED irreversibile.
- Chiamate con altro lock detenuto o contesto assente rifiutate;
  IF=0/1 conservato. Contesto ottenuto a IRQ disabilitati. Una richiesta
  ancora accodata resta conteggiata dopo il fallimento del consumer.
- Quattro report cppcheck della suite e quello specifico del gateway
  senza error/warning/performance/portability. Dopo la suite sono stati
  aggiunti i negativi overflow e retention, l'accounting degli handle e
  reso esplicito lo storage opaco come byte allineati:
  gateway debug/release e cppcheck ripetuti sul test finale.

Prove specifiche finali in `kernel/build/c5-requests-gateway-final.log`
e `kernel/build/c5-requests-gateway-final/`. La review iniziale ha corretto
la finestra fra scelta del contesto e acquisizione e due incrementi di
contatori dentro assert rilevati da cppcheck. Il requisito `no_std` ora
appartiene alla crate FFI, mentre il nucleo e un modulo safe riutilizzato.

Il modulo non alloca heap. I cinque handle del test concorrente sono
chiusi e conteggiati esplicitamente; il totale handle del processo puo
variare per il runtime host e non viene dichiarato prova di leak zero.
ASan strumenta il C, non il codice Rust o la RAM fisica; Valgrind non
eseguito e coverage linee/branch N/D.

Non ancora collegato al worker nativo, ai produttori AP o al terminale
panic del kernel. Nessuna modifica al boot e nessuna immagine kernel
include il gateway: QEMU N/A per questo blocco host. Prima dell'uso SMP
ordinario restano binding/lifetime della sessione, wakeup/backpressure,
test integrati e dieci boot consecutivi. C5 resta aperta.

## K4-C5: checkpoint precedente del nucleo della coda

Implementato `rust/requests/queue.rs`: otto richieste possedute, massimo
256 byte ciascuna, UTF-8 verificato, FIFO, lavoro in corso separato e
stati OPEN/DRAINING/CLOSED/FAILED. Il consumer host usa take/complete:
nessuna seconda estrazione prima della conferma, nessun retry dopo errore
terminale. Una richiesta in corso puo convivere con otto richieste accodate.

Prove sulla versione finale:

- Sei test Rust debug e sei release, warning come errori e overflow checks.
  FIFO/wrap per 8.000 richieste; altre 100.000 operazioni confrontate con
  una `VecDeque` di riferimento, in ciascuna configurazione.
- Confini 0/256/257 byte, UTF-8 italiano, encoding invalidi, controlli,
  copia del payload, piena/vuota, drain, chiusura prematura, errore terminale.
- Clippy del nucleo e dei test senza warning; compilazione `no_std` in
  oggetto con panic abort. Il nucleo vieta unsafe e non usa heap o FFI.
- Suite console esistente arrivata a PASS: test C con MSVC W4/WX e ASan,
  test Rust, harness host e quattro report cppcheck senza diagnostiche
  error/warning/performance/portability. Hardware e IRQ dei test host
  restano mock, non prove su dispositivi reali.
- Layout centralizzato: include il nuovo test, nessun errore; nuovi file
  Rust di 138 e 153 righe. Diff senza trailing whitespace.

Evidenze: `kernel/build/c5-console-queue/debug.log`, `release.log`,
`kernel/build/c5-console-host.log`, report cppcheck in
`kernel/build/c5-console-host/` e `kernel/build/test-layout/layout.json`.
La suite ampia ha eseguito la stessa logica della coda; dopo la review
e stato distinto PENDING da FULL nella sola risposta di close, ripetendo
entrambi i profili Rust, Clippy e compilazione sul sorgente finale.

Limiti: nessun singleton condiviso, lock kernel, gateway C o collegamento
al worker ancora implementato. `&mut Queue` richiede accesso esclusivo;
questo codice da solo non e una coda MPSC sincronizzata. Il consumer host
verifica il protocollo, non disegna tramite il servizio C.
Nessuna immagine kernel include la coda: QEMU N/A per questo blocco host,
non dichiarato PASS o eseguito. Prima di consentire chiamate AP servono
binding/lock, wakeup e dieci boot SMP del profilo integrato.
Nessun Valgrind eseguito; ASan C non misura perdite fisiche del kernel.
Copertura linee/branch N/D. C5 nel suo insieme resta aperta.

## Checkpoint storico della console BSP

Modulo: `kernel/console/video/` e sole interfacce necessarie alla sessione
input/shell, inventario stack e gateway framebuffer. Scelta approvata:
[servizio unico con aggiornamenti RAM serializzati](shell-design.md).

Esito del checkpoint: **APPROVE per il fix idle nel profilo QEMU**.
Il blocco successivo aggiunge il cursore e corregge la chiusura PS/2:
[prove correnti e immagini finali](caret-validation.md).
Il rollover HPET riprodotto e corretto con regressioni deterministiche,
32 boot finali, verifica video e attraversamento reale del contatore.
[Report corrente, immagini e limiti](../../../../docs/validation/consolidamento.md#validazione-dei-backend-x86).
Le immagini e campagne descritte sotto sono il checkpoint della prima
integrazione console; non sostituiscono le prove successive del fix HPET.

## Comportamento implementato

Il profilo esplicito `-ConsoleShell` collega tastiera PS/2, editor italiano,
task shell separata e framebuffer. Prompt `> `, UTF-8 con accenti/euro,
Backspace, Enter, wrap e scroll. Comandi `help`, `echo`, `status`, `exit`;
output disponibile anche sulla seriale. Il transcript dei risultati resta
distinto dalla riga in modifica, conservata quando arrivano risposte.

Task0 aggiorna la riga accettata dall'editor; task2 pubblica i risultati;
task1 disegna gruppi di massimo otto celle adiacenti. La riga viene confermata
nel transcript dopo la copia in FIFO e prima del segnale alla shell, che
puo cedere il controllo. Nessuna nuova task
o coda: restano la FIFO quattro comandi e gli eventi esistenti. Observe,
recheck e wait evitano il parcheggio con lavoro dirty. Quando il video
ha lavoro pendente, la finestra IRQ Assembly esistente usa poll di 1 ms;
senza lavoro torna alla politica input precedente. Nessun disegno in IRQ.

Mutazioni RAM e gateway C restano a IF=0, sul BSP cooperativo. Non si
abilitano IRQ in C arbitrario. Il worker completa il video prima della
raccolta degli stack. Il sink output viene revocato prima di chiudere
il servizio. Video assente: sessione seriale; autorita presente ma invalida:
errore terminale. Il boot ordinario non abilita questo profilo.

## Ownership e sicurezza

- Oggetti originali e distinti, buffer con lunghezza/capacita esplicite;
  limite 256 byte per aggiornamento. UTF-8 interamente validato prima
  della mutazione. Rust decodifica scalar tramite ABI intera senza puntatori.
- Rendering esclusivo: aggiornamenti, close e rientro rifiutati durante
  un gruppo. Errore di disegno persistente, senza falso rollback dei pixel.
- Pool/root originali, stack live e lease pinned verificati; inventario
  temporaneo derivato dagli owner reali. Controlli completi prima/dopo
  ogni gruppo, snapshot originale ripristinato anche in caso di errore.
- Rifiuto di caratteri oltre capacita resta nell'editor; una riga troncata
  non viene eseguita. Nessun nuovo accesso a registri o cambiamento Assembly.

Costo fisso owner + vista: 133776 byte, senza heap; composizione O(celle
della griglia + scalar della riga). Solo il dirty viene disegnato. Le API
accettano buffer kernel fidati vivi; non validano puntatori utenti.

## Prove host eseguite

Suite console (`kernel/build/console-shell-host.log`), artefatti in
`kernel/build/console-shell-host/`:

- Rust debug/release e Clippy con warning come errori: UTF-8 valido,
  malformato, overlong, surrogate, fuori range e controlli proibiti;
  regressioni cursore e policy temporale.
- Griglia: 374635 CHECK; nuovo servizio: 60155 CHECK, 10000 cicli di
  scroll/riuso, canary, copied owner, rientro, risposte intercalate,
  rifiuti senza mutazione e close.
- Mapper nativo: 554167 CHECK; gateway live: 436; autorita: 576;
  policy HPET: 1100. CPU, PTE e autorita hardware sono mock host.
- Worker reale con confini scheduler/video mock: 7386 CHECK. Stop durante
  observe in 120 punti con gruppi ancora pendenti, piu stop gia attivo;
  tutte le celle completate prima di WorkerDone. Il test include il sorgente
  operativo, non una sua riscrittura.
- MSVC warning come errori e ASan sul C; cppcheck generale, diagnostico
  e profilo operativo, senza diagnostiche bloccanti.

Suite input/shell (`kernel/build/console-shell-input-host.log`):
C/Rust shell 220066 CHECK, incluse bind/unbind originali, sink fallito,
echo italiano e exit; bridge con backpressure, cancel e conferma video
prima del risveglio, fallimento della conferma senza segnale;
controller 526725, discovery 7989, runtime 4559,
testo 1114132, editor 2680276 CHECK. Rust debug/release, Clippy, ASan C
e cppcheck passano. Quattro test del verificatore pixel Python passano.

ASan non strumenta Rust, Assembly o la VM; accounting QEMU distinto
dal leak detector host. Nessun Valgrind disponibile/eseguito su Windows.
Coverage linee/branch N/D.

## Immagini e campagne del checkpoint console

Shell finale: `kernel/build/console-shell-final-image/kernel.elf`, SHA256
`DEEAB1007A4C8E720810BA5986043B00FCA0FBA18A815AF12F3FC74D5A26773C`.

Ordinaria: `kernel/build/console-shell-ordinary/kernel.elf`, SHA256
`30A68B1C1CF48DDCDA820CD0E7589EB56ABC5A11A5394FB0FAA490426A3721A3`.

- Campagna video intermedia: manifest (`kernel/build/ps2-input-firmware-fe0e77facc4c49f88ffa01bb3df9865b/results.json`),
  dieci boot consecutivi, 70627 celle e 10044 gruppi. Per boot: dieci righe/
  comandi, 640 pixel fisici selezionati e 786432 pixel visibili verificati.
  Budget iniziale/finale 20420 pagine in tutti i boot; nessuna perdita input
  o fault inatteso osservato, cleanup input/watchdog e KERNEL_READY.
  Immagine `81C8CC696930DA745505A1C7F7FF80CE2DFFB0E696F85E280C1EB58C3676A5CC`,
  precedente alle sole diagnostiche aggiunte al ramo di errore idle.
- Fallback seriale intermedio: dieci boot con diagnostiche (`kernel/build/ps2-input-firmware-8b1eb8d59e1642f0a0d3d706440c3c0e/results.json`)
  e venti consecutivi aggiuntivi (`kernel/build/ps2-input-firmware-07f83bfb8d1048178b60fd9091ff77b7/results.json`)
  passati sull'immagine `55F18BC2F7C168F92E5D5187394478FA863F0724EFC3D0D3A79B38BD45E38A98`.
- Stress sull'immagine finale: manifest (`kernel/build/ps2-input-firmware-b195fd9a860440c5aaf064c1c7ac5f8c/results.json`),
  un boot senza video, 612 pressioni, 1860 byte, 12 righe/comandi, overflow
  rifiutato e Backspace. Budget 20465 pagine ripristinato, cleanup e KERNEL_READY.
- Fallback finale: manifest (`kernel/build/ps2-input-firmware-c60dc5b95da945cdb98611d3434d6a6a/results.json`),
  dieci boot consecutivi della stessa immagine finale, input/shell seriali,
  marker SERIAL_CLEAN e assenza del sink video; cleanup e budget 20465
  ripristinato.
- Ordinaria finale: report (`kernel/build/framebuffer-live-e0a60f2c2c204a4994ffc1ac3cbd965a/console-grid-results.json`),
  dieci boot consecutivi dell'immagine 30A68B...; nessun probe console RAM
  diagnostico, loader invariato e KERNEL_READY. Trenta boot finali complessivi.

Video sull'immagine finale: manifest (`kernel/build/ps2-input-firmware-8f777c9f936147829dd9779654a9845b/results.json`),
dieci boot consecutivi, 70374 celle e 9987 gruppi, 640 pixel fisici e
786432 visibili verificati per boot, budget 20420 ripristinato. Massimo
276192 tick PM, circa 77,16 ms; sei boot con massimo sopra 10 ms.
Il gate 100 ms passa, l'obiettivo 10 ms non e stabile. Non confondere queste
misure TCG con una garanzia hardware o end-to-end.

Review di struttura e link: manifest (`kernel/build/console-render-review.json`),
massimo sorgente C/Rust/header nel perimetro 397 righe, nessuna cartella
funzionale verificata oltre cinque sorgenti. Layout dei test centralizzato
e diff senza errori di whitespace. `reuse/` non modificato; nessun commit,
push o merge eseguito in questo blocco.

Campagna video intermedia: massimo 47901 tick PM, circa 13,38 ms. Tre boot hanno un
massimo sopra l'obiettivo 10 ms: l'obiettivo non e raggiunto stabilmente.
Il gate 100 ms passa; queste misure TCG non certificano latenza hardware
o end-to-end. La campagna RAM precedente e il singolo boot di debug pixel
non sostituiscono i dieci avvii dell'immagine finale.

Il verificatore ricostruisce il transcript dai comandi/risposte seriali,
simula wrap/scroll indipendentemente dal C/Rust e confronta lo schermo.
Legge inoltre celle selezionate direttamente dalla memoria fisica della VM.
Ogni boot usa ESP/variabili OVMF private, hash, deadline e VM posseduta.

## Correzioni durante il ciclo

La prima versione UTF-8 dipendeva dal runtime Rust: sostituita con decoder
intero freestanding e test esaustivi. Corretti fixture C con warning signed
char, mock di gruppo che dereferenziava ticks nullo e lunghezze dei literal
shell rese esplicite con sizeof. Due aspettative del test Python erano
errate (spazi finali e scroll su griglia 3x2): corrette, quattro test passati;
nessuna modifica al renderer per adattarlo a quelle aspettative.

La prima campagna finale ha passato nove boot e fallito il decimo durante
la chiusura: `kernel/build/ps2-input-firmware-f26132a42dc040bba474a2dab5f10fe7/boot-10/serial.log`,
vecchio ELF SHA256 `F09D16ED28D30E3996C6897E22AA809CDB836DDA2A8E348587BDD078C2917BA7`.
Non e un gate passato. Observe cede il controllo: uno stop arrivato durante
quella richiesta faceva uscire il worker con dirty residuo. Corretto il
ramo successivo per tornare al drain; aggiunto il test deterministico sopra.
La campagna dell'immagine corretta riparte da zero. Nessuna risorsa fisica
persa nel tentativo fallito: budget ripristinato, ma presentazione incompleta
giustamente respinta dal controllo di chiusura.

La successiva immagine `6EAC5FA97D0AFC319B6A10481BD74D4357E32967C30BDA036ABB7BBDCF5A8418`
ha passato dieci boot: `kernel/build/ps2-input-firmware-4c6a413c324546079ddb9f40d06b6ff6/`.
In review finale la conferma riga e stata anticipata al segnale dati per
evitare che una risposta/exit possa precedere il commit della riga sul video.
Test dedicato sul bridge; queste prove intermedie non sostituiscono quelle
dell'ELF finale indicato sopra.

Il fallback senza display sull'immagine `81C8...` ha passato cinque boot
e fallito il sesto nel ramo idle del dispatcher, codice 2:
`kernel/build/ps2-input-firmware-d7e69479bbba496eb752d65d7e2770fd/boot-6/serial.log`.
Non indica il limite totale di passi (codice 3). Aggiunte diagnostiche di
passi, attese, failure e risultato watchdog. Le diagnostiche finali sono nel
backend PS/2, con distinzione state/watchdog/drain; il file d'integrazione
scheduler resta a 397 righe. Nessuna soglia allargata e
nessuna sequenza hardware cambiata in quel checkpoint. La causa allora
rimaneva aperta: una campagna successiva verde non era da sola una correzione.
L'indagine e il fix successivi sono separati nel report corrente linkato sopra.

Passo richiesto al checkpoint: riprodurre il ramo idle
con le diagnostiche locali e distinguere state, attesa watchdog e drain IRQ.
La possibile presenza di un IRQ tastiera pendente al controllo finale LAPIC
e soltanto un'ipotesi, non la causa dimostrata. Non rimuovere controlli o
alzare soglie per ottenere PASS; prima servono stato e prova riproducibile,
poi correzione circoscritta e nuova campagna sull'immagine risultante.

## Riproduzione

Dalla radice del progetto, PowerShell 7, Python e toolchain supportate:

```powershell
./arch/x86/boot/uefi/scripts/build.ps1 -BootKernel
./kernel/scripts/build.ps1 -ConsoleShell -OutputDirectory ./kernel/build/console-shell-final-image
python -B arch/x86/kernel/drivers/input/ps2/scripts/firmware.py --image kernel/build/console-shell-final-image/kernel.elf --boots 10 --text --shell --console-shell
python -B arch/x86/kernel/drivers/input/ps2/scripts/firmware.py --image kernel/build/console-shell-final-image/kernel.elf --boots 10 --text --shell --console-shell --video-absent
```

Le campagne sono automatiche e non installano l'immagine nella ESP ordinaria.
Usare il Python/QEMU effettivamente installato. I risultati includono seriale,
trace IRQ, screenshot e manifest; la build da sola non dimostra il gate.

## Limiti

QEMU q35/TCG/OVMF, BSP, BGR; RGB coperto host. Nessuna prova su hardware
fisico, SMP, preemption, userspace o GUI. Sessione ancora circoscritta dalle
guardie input/watchdog v0; `exit` chiude e raggiunge KERNEL_READY, non spegne
il PC. Gate temporale gruppo 100 ms, obiettivo 10 ms misurato separatamente;
nessuna garanzia end-to-end o watchdog per uno stallo C a IF=0. Font bitmap
offline, niente TTF runtime, shaping o composizione Unicode.
