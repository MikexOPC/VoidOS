# Decoder mouse: review e prove dell'11 ottobre 2026

Checkpoint del primo blocco, precedente alla preparazione AUX.
Stato successivo: [trasporto condiviso](controller-validation.md).

Esito: decoder approvato per le prove host; driver mouse non ancora pronto.
Il contratto e i gate restanti sono nel [design](design.md).

## Implementato

Parser Rust senza unsafe, tre byte per evento, interfaccia scalare C/Rust,
wrapper C con controlli NULL/overlap, delta signed e reset esplicito.
Overflow rifiuta l'intero pacchetto; nessun evento prima del terzo byte.
Stati serializzati e byte a larghezza piena vengono validati prima dell'uso.
Nessuna allocazione o accesso hardware nel nuovo codice.

## Evidenze finali

Suite eseguita tramite `kernel/drivers/input/ps2/scripts/test.ps1`:
log `kernel/build/mouse-ps2-suite.log`, exit0.
Output `kernel/build/ps2-decoder-90d8e5530fbb455ca9fc95267b1834a0/`;
prove mouse nella sottocartella `mouse/`, hash dei sei sorgenti verificati
in `mouse/sources.json`, compilazione e risultati in `mouse/tools.log`.

- Rust:7 casi distinti, tutti passano sia debug sia release;
  2.097.152 pacchetti per esecuzione del caso esaustivo,100.000 transizioni
  deterministiche avversarie, stati riservati, payload FA/FE/AA, overflow,
  reset di ogni prefisso e byte oltre255.
- C/Rust ABI:2.097.152 pacchetti, confronto indipendente del segno,
  tre gruppi aggiuntivi per errori/reset/overlap, dimensioni4/12 byte
  verificate dal compilatore. ASan sui sorgenti C, MSVC `/W4 /WX`.
- Rust warnings e Clippy trattati come errori. Rustc1.98.1;
  oggetto no_std con `panic=abort`. Dumpbin non mostra simboli UNDEF
  nell'oggetto finale; esportazione `kernel_ps2_mouse_decode` presente.
- Cppcheck2.22.0: nessuna diagnostica bloccante. Rimangono informazioni
  sui tre header standard non risolti e sul numero di checker attivi;
  non viene dichiarata una scansione priva di ogni messaggio.
- Regressioni esistenti: decoder tastiera, layout italiano, editor,
  shell/coda, bridge, controller509.433 controlli, discovery7.989,
  route/IRQ mock4.559. Tutte passano; mock non equivalgono a hardware.
- Verifica strutturale:54 sorgenti nella pipeline PS/2, LF, bounds
  dimensionali, inclusioni/link locali; `git diff --check` passa.

Il runner PS/2 ora invoca anche la suite mouse; non occorre avviarla
separatamente. Per il solo decoder: `kernel/drivers/input/ps2/scripts/mouse/test.ps1`.

## Revisione manuale

Ogni transizione ha lavoro costante; nessun indice dinamico o dereference
Rust. C converte i delta tramite sottrazioni entro int32, evitando cast
unsigned-to-signed fuori intervallo. Il controllo overlap usa differenze
uintptr senza sommare lunghezze a indirizzi. I puntatori C restano prestiti
kernel fidati: non è una validazione di puntatori userspace.

Il reset non garantisce riallineamento del flusso. Il futuro consumer
deve trattare perdita/malformed come failure di sessione e invalidare i
pulsanti, senza cercare automaticamente un nuovo header. La politica
fisica futura e le quirk Linux non sono dedotte dai soli test esaustivi.

## Limiti e gate aperti

Nuovi boot QEMU mouse:0. Il codice non è ancora collegato all'immagine
kernel; i22 boot del blocco cursore restano evidenza di quel blocco,
non del driver mouse. Non sono ancora implementati negoziazione AUX,
IRQ12, routing condiviso, task mouse o puntatore grafico.

ASan strumenta C, non l'oggetto Rust. Nessun heap nel modulo:
accounting locale senza allocazioni, non misura leak RAM fisica.
Valgrind non eseguito sulla toolchain Windows; coverage linee/branch N/D.
Hardware fisico, SMP, input USB, rotella e quirks non certificati.

Restare nello stesso modulo fino a trasporto condiviso, integrazione e
dieci boot finali con input combinato, stop/cleanup e budget verificati.
Nessuna dichiarazione READY TO PROCEED per il driver completo.
