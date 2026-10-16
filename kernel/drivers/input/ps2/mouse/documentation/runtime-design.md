# Ricezione mouse: profilo diagnostico separato

Checkpoint del bring-up separato. Il blocco successivo unisce mouse e
console video, senza puntatore grafico: [integrazione e gate correnti](../../../../../console/video/documentation/mouse-integration.md).
Le descrizioni di esclusione del renderer sotto valgono per il profilo iniziale.

Decisione utente: quarto stack e vettore42h, estensione circoscritta
degli adattatori scheduler/IRQ/watchdog. Il nuovo profilo `-Ps2Mouse`
non abilita il renderer: i profili tastiera e console restano separati.

Ownership: stesso controller; due route MADT distinte per IRQ1/IRQ12,
vettori41h/42h, due subscription e due code256 byte. Eventi4/6,
consumer task0/3; task1 riceve IRQ nella finestra Assembly verificata,
task2 esegue la shell. Nessuna nuova finestra C con IF1.

Quattro lease pinned16 pagine, distinti e senza overlap:64 pagine
temporanee, restituite solo dopo stop/ack/unbind e fine accounting.
Il registro IRQ accetta esattamente40h/41h/42h; frame kernel IF1;
altri vettori non diventano handler hardware autorizzati.

Schema non eseguibile: prepare entrambi -> route masked -> subscription
e code -> quattro task/accounting -> enable reporting -> route unmasked
-> task consumer -> stop richiesto -> mask entrambe -> fermare dispositivi
-> drain IRQ/EOI -> unbind -> svuotare code -> collect -> budget originale.

Ogni IRQ legge status prima di data e instrada il byte secondo AUX bit5,
non secondo il vettore. Solo il vettore entrato riceve EOI dopo verifica
ISR. Handler bounded32byte, nessuna allocazione, decodifica o output.
La coda piena rifiuta il nuovo byte, produce LOSS e richiede stop pulito.
Decoder invalido/overflow non genera movimenti; perdita invalida pulsanti
e stato parziale, nessuna ricerca automatica del prossimo header.

Preparazione e attivazione avvengono con IRQ masked e sorgenti ferme.
Shutdown: prima disable delle interfacce e drain, poi reset FF mouse
(FA AA 00) e F5 tastiera. QEMU reset azzera la coda mouse prima dell’ACK;
un semplice F5 lascerebbe payload FA indistinguibili dalla risposta.
Mode/readback e CLOSED solo dopo conferma. Timeout o protocollo errato
mantengono DIRTY/fail-stop, senza liberare stack o subscription vive.
Il profilo iniziale richiede mouse standard: fallback opzionale runtime
non viene dichiarato finché manca il suo test. Tastiera sola resta un profilo.

Fonti: [QEMU v10.1.0 pckbd.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/pckbd.c)
`kbd_update_irq_lines`, `kbd_read_data`, definizioni mode/status e D4;
[ps2.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/ps2.c)
`ps2_write_mouse`, reporting F4/F5 e reset;
[Intel SDM Vol3 §6.12 e §10.8.5](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
per frame/interrupt gate ed EOI del vettore ISR; Intel82093AA §3.2.4
come nel backend route esistente (vector7:0, polarity13, trigger15, mask16).
QEMU installato11.1.0: round trip e input nel binario sono prove separate
dal sorgente10.1.0, non una certificazione hardware fisico.

Accettazione: host su route12/alias, registrazione42h, frame IF/ring,
quarto lease/rollback, trasporto start/stop e dispatch canali; decoder già
esaustivo. Dieci boot del nuovo profilo con input combinato, delta e
pulsanti confrontati con input host, eventi su task IRQ, idle e cleanup.
Regressioni tastiera e ordinario; perdita intenzionale distinta da crash.
Nessun puntatore grafico prima della chiusura di questi gate.
