# Mouse e console nella stessa sessione

Decisione utente, 11 ottobre 2026: integrare prima i profili esistenti;
il puntatore grafico appartiene al blocco successivo. Modulo console video,
con soli adattatori PS/2, build e test necessari; fix watchdog autorizzato
dopo il panic. Stato: implementato e verificato, 12 ottobre2026.
La prima campagna passa otto boot e fallisce nel nono; dopo la correzione
watchdog i dieci boot consecutivi finali e lo stress passano.

## Ricerca e alternative

[Linux v6.12 fbcon, Attaching, Detaching and Unloading](https://www.kernel.org/doc/html/v6.12/fb/fbcon.html)
descrive console e framebuffer come livelli distinti, con dipendenze di lifetime.
Qui manteniamo questa separazione, senza copiare codice o introdurre locking SMP.
Il [contratto mouse](../../../drivers/input/ps2/mouse/documentation/runtime-design.md)
resta valido per controller, IRQ, code e decoder; nessun nuovo accesso hardware.

A, scelta: combinare `-Ps2Mouse -ConsoleShell`, con task0 tastiera,
task1 worker IRQ/rendering, task2 shell e task3 mouse. Il mouse produce
ancora eventi seriali; la console conserva testo e cursore a sottolineatura.

B: aggiungere subito overlay del puntatore, coordinate, ripristino dello
sfondo e invalidazione dopo scroll. Richiede un ulteriore contratto di
rendering e test pixel; rinviato finche la sessione combinata non passa.

## Ownership, risvegli e chiusura

- Due code payload indipendenti, eventi4/6, una sola sorgente hardware
  associata a ciascuno. Segnali software della console cambiano la generazione
  dell'evento atteso dal worker; non pubblicano byte mouse o nuove subscription.
- Worker su evento6 nel profilo mouse, su evento4 nel profilo tastiera.
  Anche input testuale e output shell segnalano lo stesso evento del worker.
  Un risveglio aggiuntivo della task mouse e ammesso: ricontrolla la sua coda.
- Observe/recheck/wait e controllo dirty restano obbligatori. Un segnale
  avvenuto prima di WAIT non deve lasciare il renderer sospeso con lavoro pendente.
- Un solo renderer, gruppi di massimo8 celle, controlli completi per gruppo.
  Ritardi validi restano WARN; errori di disegno, ownership e clock terminali.
- Quattro stack pinned distinti, 64 pagine. Il backend video verifica i lease
  effettivi; il numero di pagine da solo non autorizza il mapping.
- Stop comune: mask/stop/drain di entrambi i dispositivi, revoca dei token,
  completamento dei consumer, svuotamento del video, chiusura console,
  raccolta stack e budget iniziale ripristinato. Una task mouse viva o uno
  stato mouse residuo impediscono la chiusura console.
- Nessun disegno negli IRQ; IF=0 per mutazioni RAM/rendering sul solo BSP.

Sequenza non eseguibile:

```text
prepare controller + console -> quattro task -> reporting e IRQ
IRQ -> coda tastiera oppure mouse -> consumer dedicato
tastiera/shell -> mutazione RAM -> segnale evento worker
worker -> verifica lease -> gruppo video -> finestra IRQ -> yield/wait
exit -> stop comune -> drain consumer/video -> revoke -> collect -> budget
```

## Accettazione

Host: entrambe le varianti worker, eventi corretti, fallimenti invariati,
chiusura negata con mouse vivo/token/route/decoder/pulsanti/perdita residui,
64 pagine e rollback del backend live. Warning come errori, ASan, Rust e
analisi statica sui profili pertinenti.

QEMU: dieci boot consecutivi della stessa immagine combinata; movimento,
tre pulsanti, tasti intercalati, shell italiana, pixels/transcript/caret,
IRQ41h/42h, code senza perdita e budget ripristinato. Stress prolungato
e fallback seriale senza video; regressioni dei profili separati.
Coverage N/D; prove QEMU non certificano PS/2 su hardware fisico.

## Checkpoint di debug

Immagine `kernel/build/console-mouse-current/kernel.elf`, SHA256
`F142EA63EE2D5EF5BC7D1831E17FFAFCA7CF3FBB58326071868EECAFDD282653`.
Campagna `kernel/build/ps2-input-firmware-1acf9624090748d4a3cff52435d5ddce/`:
boot1..8 verificano input, mouse, shell, pixels, IRQ e cleanup; boot9
si ferma durante `echo ciao...`, prima del cleanup. Evidenza in
`boot-9/serial.log`, `monitor.log`, `qemu.log`; runner termina solo la VM posseduta.

Marker: `PS2_IDLE_FAILURE_STAGE=2`, `PS2_WATCHDOG_FAILURE=14`,
`PS2_WATCHDOG_WAKE_DETAIL=6`, `PS2_WATCHDOG_RESULT=3`,
`SCHEDULER_DISPATCH_FAILED`; ISR vuoto, IRR65 nel dump APIC.
Questi dati localizzano il fallimento nel percorso idle/watchdog;
nel checkpoint la causa non era ancora dimostrata. Nessun fail-stop renderer osservato.
Gli otto successi non sostituiscono dieci boot consecutivi finali.

## Causa e correzione watchdog

Estensione circoscritta autorizzata dall'utente dopo il panic. Il controllo
post-wake usava `x86_lapic_idle`, che richiede ISR e IRR completamente vuoti.
La sorgente input era ancora attiva: un nuovo IRQ41h poteva restare in IRR
al ritorno IF0 della finestra Assembly, dopo l'ack della deadline HPET.
Si applicava quindi una precondizione di revoca a una sessione operativa.

[Intel SDM revisione089, Vol3 §12.8.4/Fig12-20](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf)
descrive IRR come richieste accettate e non ancora servite, ISR come richieste
in servizio. ISR100h..170h e IRR200h..270h sono otto banchi da32 bit a passo16;
i vettori41h/42h corrispondono ai bit1/2 del banco2.

Il nuovo controllo resta bounded: registro subscription valido e non Busy,
token live per ciascun vettore input ammesso, ISR vuoto, IRR limitato a tali
vettori. Niente EOI per richieste solo pendenti, niente clearing dei registri,
nessuna nuova finestra C con IF1. IRQ40h pendente dopo ack, IRQ estranei,
ISR occupato, token invalido, registro corrotto e lettura MMIO fallita restano
terminali. La quiescenza completa prima di unbind/cleanup non cambia.

Regressione host prima del fix: caso560 fallisce in
`kernel/build/watchdog-host-d0e38b01d2b04a1a901eeb469fd99a90/`.
Dopo il fix: `kernel/build/watchdog-host-477384aab81e4f4bb06d1121c12ad95b/`,
suite C/ASan e Rust passate, undici nuovi scenari60..70 ripetuti100 volte.
Due casi positivi: IRQ41h, IRQ41h+42h con token live; nove negativi su ownership,
ISR/IRR, errori MMIO e registro. La suite finale aggiunge il rifiuto della
revoca con token/risorse conservati fino a quiescenza: passa in
`kernel/build/watchdog-host-8f697584233e4c82a53c6dbc5aa28d0c/`.
CPU/APIC/HPET restano mock in questa prova.
Il primo tentativo di fixture usava il modo44 gia occupato da un test rollover;
non conta come regressione pertinente. I nuovi scenari usano identificatori distinti.

La nuova immagine combinata e `kernel/build/console-mouse-final/kernel.elf`,
SHA256 `D8FA742BCAA029C635F72CED5138A7679734C0EF67548E164E6C8146921674DB`.
Dieci boot consecutivi finali e stress passati sulla stessa immagine.

## Prove finali

| Prova | Risultato | Evidenza in `kernel/build/` |
| --- | --- | --- |
| Console host C/ASan, Rust debug/release/Clippy, Python | PASS; 19 test Python; worker8833 controlli per profilo; wake/close6010 tastiera e6016 mouse | `console-mouse-host-final/`, `.log` |
| PS/2 host C/ASan e Rust | PASS; controller mouse169985 controlli, task53000/5000 cicli, decoder esaustivo e regressioni tastiera/shell | `ps2-decoder-99ebb765d9ec45d198000e87c015b087/`, `console-mouse-ps2-host.log` |
| Watchdog prima/dopo | Prima FAIL560; dopo PASS100 ripetizioni, inclusi11 nuovi scenari e revoca negata | Directory watchdog sopra, `console-mouse-watchdog-before-final.log`, `console-mouse-watchdog-final.log` |
| QEMU combinato | PASS10/10:98 tasti,10 comandi,35 eventi/105 byte mouse per boot; perdite mouse0 | `ps2-input-firmware-acd624adf7454dfc9b21b18417aee699/` |
| QEMU stress combinato | PASS:812 tasti,778 scalar,12 righe/comandi, una riga piena respinta;35 eventi mouse | `ps2-input-firmware-282791b0dcf94725b25cffd0e8ced217/` |
| QEMU senza video | PASS:98 tasti,10 comandi,35 eventi mouse e fallback seriale | `ps2-input-firmware-d80e8b5a9f1e4b98bdc3009c54589f71/` |
| Regressione console senza mouse | PASS:98 tasti,10 comandi, pixels/caret e budget ripristinato | `ps2-input-firmware-9cb141d8051543ffa46893cc454a6cb4/` |

Ogni boot video verifica768 pixel fisici e786432 pixel visibili, transcript
e caret, IRQ41h/42h, cleanup e `KERNEL_READY`. Nei dieci boot il massimo
misurato e155192 tick PM, Overruns0; nel test lungo63967 celle/8515 gruppi,
MaxPm56721 e Overruns0. Budget20417 prima/dopo per video;20462 senza video.
I valori di budget dipendono dall'immagine e non sono quote hardware universali.
Totale13 boot finali positivi:12 sulla combinazione (dieci consecutivi,
stress, video assente) e uno sulla regressione console con sola tastiera.
Immagine della regressione `kernel/build/console-keyboard-final/kernel.elf`,
SHA256 `FBD53A874766D63330D0905B4E8480FF9B0F86F914E6083C81E6D48FA1835761`.

Cppcheck2.22: suite console per entrambi i profili e scansioni dedicate
`console-mouse-production-static.xml`, `console-mouse-watchdog-final-static.xml`
senza errori/warning/performance/portability bloccanti. Include di sistema
non forniti sono diagnostica informativa. La scansione preliminare che univa
worker e mock al codice operativo segnala quattro nullPointer su boundary
vietati dai test (stub che chiamano assert e restituiscono NULL); queste
segnalazioni restano conservate in `console-mouse-static.xml`, distinte
dalla scansione del codice realmente operativo.

Review strutturale console e adattatori: massimo398 righe per sorgente,
nessuna violazione; script build399 righe. Watchdog e test:24 sorgenti, massimo293 righe,
nessuna cartella oltre5 sorgenti, LF e whitespace verificati. Diff controllato
nel perimetro; modifiche esterne dell'utente non vengono normalizzate.

## Valutazione e limiti

L'integrazione condivide il worker e conserva due code e decoder distinti;
nessuna nuova allocazione o subscription software. Risvegli aggiuntivi della
task mouse sono ammessi e richiedono il normale recheck della sua coda.
Le scansioni LAPIC sono bounded16 letture, piu validazione dei token; nessun
benchmark di overhead scheduler o confronto prestazionale universale.

ASan strumenta il C host; Rust/Assembly/kernel fisico non sono coperti da
ASan. Valgrind non eseguito; coverage linee/branch N/D. Accounting fisico e
risorse della sessione sono verificati in QEMU, senza promettere zero leak
su tutto il kernel o compatibilita con PC fisici.

Il prossimo blocco e il puntatore software: posizione e limiti, ripristino
dello sfondo, ridisegno dopo output/scroll, sempre tramite un solo worker.
Non sono introdotti finestre, compositore, rotella, userspace o SMP.
Review del blocco: APPROVE per il profilo QEMU/BSP verificato; nessun gate
di certificazione hardware fisico o produzione viene dichiarato completato.
