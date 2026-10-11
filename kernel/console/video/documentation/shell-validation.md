# Shell video operativa — verifica, 11 ottobre 2026

Modulo: `kernel/console/video/` e sole interfacce necessarie alla sessione
input/shell, inventario stack e gateway framebuffer. Scelta approvata:
[servizio unico con aggiornamenti RAM serializzati](shell-design.md).

Esito corrente: **APPROVE per il fix idle nel profilo QEMU**.
Il rollover HPET riprodotto e corretto con regressioni deterministiche,
32 boot finali, verifica video e attraversamento reale del contatore.
[Report corrente, immagini e limiti](../../../../arch/x86/kernel/timer/hpet/watchdog/documentation/idle/rollover.md).
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

[Suite console](../../../build/console-shell-host.log), artefatti in
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

[Suite input/shell](../../../build/console-shell-input-host.log):
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

- Campagna video intermedia: [manifest](../../../build/ps2-input-firmware-fe0e77facc4c49f88ffa01bb3df9865b/results.json),
  dieci boot consecutivi, 70627 celle e 10044 gruppi. Per boot: dieci righe/
  comandi, 640 pixel fisici selezionati e 786432 pixel visibili verificati.
  Budget iniziale/finale 20420 pagine in tutti i boot; nessuna perdita input
  o fault inatteso osservato, cleanup input/watchdog e KERNEL_READY.
  Immagine `81C8CC696930DA745505A1C7F7FF80CE2DFFB0E696F85E280C1EB58C3676A5CC`,
  precedente alle sole diagnostiche aggiunte al ramo di errore idle.
- Fallback seriale intermedio: [dieci boot con diagnostiche](../../../build/ps2-input-firmware-8b1eb8d59e1642f0a0d3d706440c3c0e/results.json)
  e [venti consecutivi aggiuntivi](../../../build/ps2-input-firmware-07f83bfb8d1048178b60fd9091ff77b7/results.json)
  passati sull'immagine `55F18BC2F7C168F92E5D5187394478FA863F0724EFC3D0D3A79B38BD45E38A98`.
- Stress sull'immagine finale: [manifest](../../../build/ps2-input-firmware-b195fd9a860440c5aaf064c1c7ac5f8c/results.json),
  un boot senza video, 612 pressioni, 1860 byte, 12 righe/comandi, overflow
  rifiutato e Backspace. Budget 20465 pagine ripristinato, cleanup e KERNEL_READY.
- Fallback finale: [manifest](../../../build/ps2-input-firmware-c60dc5b95da945cdb98611d3434d6a6a/results.json),
  dieci boot consecutivi della stessa immagine finale, input/shell seriali,
  marker SERIAL_CLEAN e assenza del sink video; cleanup e budget 20465
  ripristinato.
- Ordinaria finale: [report](../../../build/framebuffer-live-e0a60f2c2c204a4994ffc1ac3cbd965a/console-grid-results.json),
  dieci boot consecutivi dell'immagine 30A68B...; nessun probe console RAM
  diagnostico, loader invariato e KERNEL_READY. Trenta boot finali complessivi.

Video sull'immagine finale: [manifest](../../../build/ps2-input-firmware-8f777c9f936147829dd9779654a9845b/results.json),
dieci boot consecutivi, 70374 celle e 9987 gruppi, 640 pixel fisici e
786432 visibili verificati per boot, budget 20420 ripristinato. Massimo
276192 tick PM, circa 77,16 ms; sei boot con massimo sopra 10 ms.
Il gate 100 ms passa, l'obiettivo 10 ms non e stabile. Non confondere queste
misure TCG con una garanzia hardware o end-to-end.

Review di struttura e link: [manifest](../../../build/console-render-review.json),
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
