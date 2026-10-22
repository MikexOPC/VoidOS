# Presentazione del cursore — 16 ottobre 2026

## Segnalazione e perimetro

La prova manuale dopo visual-aa segnala movimento a scatti e deformazione
temporanea. I precedenti screenshot a disegno concluso non certificavano
la stabilità durante il movimento. Il SVG è già rasterizzato offline:
sostituirlo con la stessa sagoma disegnata a mano non corregge la presentazione.

Nel percorso precedente il framebuffer visibile riceveva prima lo sfondo
del vecchio cursore, poi il testo, poi il nuovo cursore. Ogni aggiornamento
concluso emetteva anche un record seriale con cinque valori; ciascun evento
mouse emetteva la propria traccia. Sono cause concrete di lavoro aggiuntivo
e stati intermedi visibili, compatibili con la segnalazione; la sola foto
non permette di attribuire ogni difetto percepito a una causa unica.

Scelta utente: composizione delle piccole aree in RAM e tracce dettagliate
separate dal profilo interattivo. Sagoma e testo antialias invariati.

## Contratto

Una sola transazione BSP/IF=0, un solo mapping framebuffer autorizzato,
controllo completo prima del gruppo e verifica dell'autorità dopo:

1. Acquisire i tre rettangoli: vecchio cursore, testo fino a otto celle,
   nuovo cursore. Area totale massima 1944 pixel.
2. Leggere il nuovo sfondo, sostituendo le sovrapposizioni con il backup
   del vecchio sfondo e il testo aggiornato. Mai acquisire il vecchio cursore
   come parte del nuovo sfondo.
3. Comporre in RAM il contenuto finale di tutti i rettangoli. Anche le aree
   sovrapposte contengono già il nuovo cursore: nessuna cancellazione o
   composizione parziale viene deliberatamente inviata al framebuffer.
4. Confrontare i pixel finali con il contenuto precedente noto: nuova area
   acquisita e vecchio sfondo composto con la sagoma precedente. Copiare
   soltanto le sequenze cambiate; le sovrapposizioni sono scritte una volta.
   Per testo il cui contenuto precedente non è noto, scrivere senza saltare.
   Validare ogni sequenza, prevalidare tutti i puntatori, copiare gli store UC32
   e verificarne il readback. Nessuna nuova politica di cache o registro.
   Fonte del formato: UEFI 2.10 §12.9 GOP, RGB/BGR a32 bit, stride in pixel.
5. Verificare l'autorità e pubblicare il nuovo backup soltanto dopo successo.
   Dopo errori il mapping resta trattenuto; abort ammesso soltanto senza store.

Staging statico di7776 byte, originale acquisito di1296 byte e maschera dirty
di1944 byte; nuovo sfondo temporaneo di1296 byte sullo stack.
Zero allocazioni aggiunte. Massimo2268 operazioni logiche su framebuffer,
4212 accessi includendo readback nel caso massimo. API per righe: larghezza
massima72 pixel, capacità sorgente in parole32 bit, RAM kernel fidata e
stabile fino al ritorno. Copie della transazione, alias con metadati/video,
righe fuori rettangolo e budget esaurito vengono rifiutati.

`-InteractiveInput` richiede `-Ps2Mouse -ConsoleShell`. Sopprime soltanto
le tracce per evento mouse e per frame; conserva contatori, errori, log di
chiusura e un marker iniziale dopo il primo disegno completo. Il profilo
diagnostico mantiene tutte le tracce. Il launcher usa quello interattivo.

## Prove e limiti

Test C/ASan: interpolazione, RGB/BGR, padding,64 spostamenti tra aree
di testo per ripetizione,6400 complessivi, sovrapposizione e hide/restore;
API per righe positiva e rifiuti di capacità, indice riga e alias.
Suite console C/ASan, Rust debug/release/Clippy e cppcheck passate.

Checkpoint prima dell'eliminazione delle copie ridondanti — immagine diagnostica: SHA256
`026E488345CCC8EF50E00608328AD50968329CC0151B7996875DEB9CD6D304A0`.
Dieci boot consecutivi,60 scene e6000 invii continui passano;
`kernel/build/pointer-staged-qemu.log`, risultati in
`ps2-input-firmware-7f11024cd6564e52a50419b53a382b16/results.json`.

Checkpoint interattivo iniziale: SHA256
`71468FFC7C0EB38EEACFDEDA3593BB1198964C7C94C03E4F603C3E286E45E08A`.
Il collaudo dedicato confronta l'intera schermata dopo raffiche di movimenti
unitari, i324 pixel fisici del cursore, posizione finale, IRQ12, assenza di
tracce per evento/frame, help, cleanup e ripristino del budget.

Nel checkpoint interattivo iniziale dieci boot passano, ma l'audit delle
159 catture (incluse quelle prima dell'assestamento) trova157 sagome complete
e2 parziali, nessuna assente. Questo ha motivato la copia delle sole sequenze
cambiate e l'eliminazione delle riscritture sovrapposte. Evidenza storica:
`pointer-interactive-check-6eb3710061654312b7c07d2c2c887577/shape-audit.json`.
I tempi fino allo screenshot finale includono cattura host e assestamento
input, con massimo406ms in quel checkpoint: non sono latenza del renderer.

## Campagna finale con sequenze dirty

- Diagnostica: `kernel/build/pointer-dirty-diagnostic/kernel.elf`, SHA256
  `920EF3D1FB67BD58C0B8FEBFC712FECFB8178DC35F432970628C8299CBED0387`.
  Dieci boot consecutivi: `pointer-dirty-diagnostic-qemu.log`, risultati in
  `ps2-input-firmware-03d69e69309040079a1237e9b73bf237/results.json`.
- Interattiva: `kernel/build/pointer-dirty-interactive/kernel.elf`, SHA256
  `E8B116ACF0215BD6650A47EE0422FCCBCB2955B0CBE4CABDF568F94FD20E751A`.
  Dieci boot consecutivi: `pointer-dirty-final-interactive.log`, risultati in
  `pointer-interactive-check-fa281b8be36248189e228782048dc957/results.json`.
  6000 comandi di movimento,120 scene finali,135 catture totali: tutte135
  mostrano una sagoma completa, nessuna assente o parziale nel campione.
  Pacchetti emessi da QEMU e posizione guest verificati; budget20.408
  ripristinato in ogni boot. Massimo tempo fino allo screenshot250ms,
  comprensivo dell'host, non una misura di latenza input o di FPS.
- C/ASan e cppcheck: `pointer-dirty-host-final.log`; suite console C/ASan,
  Rust e Clippy: `pointer-staged-console.log` (interfacce console invariate
  nell'ottimizzazione dirty). Cppcheck del profilo interattivo finale:
  `pointer-dirty-cppcheck.log`, zero diagnostiche bloccanti.
- Scroll/riga piena: `pointer-dirty-stress.log`, risultati in
  `ps2-input-firmware-48a7b79107ef4d918285ac4356b97b0a/results.json`.
- Fallback: `pointer-dirty-fallback.log`, risultati in
  `ps2-input-firmware-d4a3fc801238444883ad5d3ebb0a4897/results.json`.
- Preparazione e avvio ESP del launcher: `pointer-dirty-launcher.log`
  e `pointer-dirty-launcher-smoke.log`; movimento, help, Esc e cleanup.

I percorsi brevi delle evidenze sono relativi a `kernel/build/`.
Le campagne hanno avuto periodi di esecuzione concorrente sullo stesso host:
nessun confronto controllato di prestazioni con le immagini precedenti.

## Tentativo fallito e oracolo input

Un primo tentativo dirty si ferma al nono boot: sagoma completa ma posizione
di un pixel diversa dal comando previsto. Non è classificato PASS.
`pointer-interactive-check-2fb72c5175644225b9c5d7297d3ccfc2/results.json`.
L'audit delle250 catture trova247 complete e3 parziali, nessuna assente.
Il tentativo non registrava i pacchetti emessi dall'emulatore, quindi non
permette di attribuire con certezza lo scarto al guest o alla consegna QEMU.

Il sorgente primario [QEMU v11.1.0 hw/input/ps2.c](https://github.com/qemu/qemu/blob/v11.1.0/hw/input/ps2.c)
definisce una coda da16 byte: `ps2_mouse_send_packet` può rinviare l'invio
per spazio insufficiente, conservando i delta fino a una successiva sync.
È una causa possibile, non una dimostrazione retroattiva del caso fallito.
L'eseguibile usato dichiara11.1.0-12130-ge470268ff4; il commit esatto non era
reperibile nel mirror durante la verifica, quindi il riferimento è al tag.

Il collaudo finale traccia `ps2_mouse_send_packet` e confronta i delta emessi
con quelli richiesti prima di accettare ogni scena. Consente sync senza
movimento se il dispositivo trattiene delta; tutte le dieci prove finali
hanno richiesto zero sync aggiuntive. Un primo avvio del nuovo oracolo ha
letto il file IRQ sbagliato: con il backend log QEMU `-trace file` redirige
anche `-d`; corretto il percorso, mantenuta la verifica degli IRQ reali.
Lo scarto precedente non si è riprodotto nella campagna finale, ma non si
afferma risolto con certezza né si rivendica assenza globale di flakiness.

Questa è composizione software con copia sul front buffer, non page flipping
o sincronizzazione vblank. I rettangoli non diventano visibili atomicamente
rispetto alla lettura del display. Gli screenshot dopo le raffiche misurano
assestamento e residui; non certificano ogni fotogramma durante il movimento,
fluidità SDL,60FPS o hardware fisico. La verifica manuale resta necessaria.
ASan riguarda il C host; Valgrind non eseguito, coverage N/D.
