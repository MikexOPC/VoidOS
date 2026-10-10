# Console con input attivo: verifica finale

10 ottobre 2026. Perimetro: console video e sole interfacce necessarie a
misurare il rendering nel profilo input esistente. [Design](measured-design.md).
Il profilo `-TestConsoleDraw` è isolato e non installabile; la shell resta
seriale. Nessun disegno negli handler, nessuna estensione della finestra
Assembly autorizzata per gli IRQ. `reuse/` intatto, ESP non aggiornata.

## Correzioni verificate

Il primo tentativo si fermava prima dell'input: inventario con 904 foglie contro
952 durante le task. I 48 frame aggiunti appartengono ai tre stack pinned.
Non è stato disabilitato il confronto: il backend controlla tutti gli slot
e restituisce il numero di pagine possedute. Il solo collaudo deriva una
fotografia temporanea, modificando esclusivamente i contatori RAM/foglie;
il renderer verifica ancora grafo, owner, MMIO, cache, guardie e alias.
Il puntatore baseline viene ripristinato anche quando il disegno fallisce.

Il misuratore C associa owner originale, flusher e clock; Rust classifica
mask, wrap, progresso e budget senza puntatori. Una sola cella per passo.
Un fallimento temporale è terminale; una cella già scritta non viene
presentata come annullata. Test di copia owner, rientro, overflow, clock
fermo, draw fallito e ripristino dell'inventario verificati su host.

## Tempi: risultato e obiettivo non raggiunto

Il gate iniziale di 10 ms è fallito al secondo boot della prima campagna:
75935 tick PM, circa 21,21 ms. Evidenza conservata in
`kernel/build/ps2-input-firmware-09cc52793b764f87be6093e058f4e18e/`.
Il primo rifiuto dell'inventario è in
`kernel/build/ps2-input-firmware-94c576e049eb4294a086c19ffe82984c/`.
Questi tentativi non sono conteggiati come campagne passate.

Il limite diagnostico finale è 357955 tick, circa 100 ms, coerente con il
budget del poll input v0; l'obiettivo di 35796 tick, circa 10 ms, resta distinto
e gli sforamenti sono contati. Fonte del clock:
[ACPI 6.6 §4.8.2.1/§4.8.3.3](https://uefi.org/specs/ACPI/6.6/04_ACPI_Hardware_Specification.html#power-management-timer-pm-tmr).
Non è una certificazione di latenza su hardware reale.

| Boot | Celle | Massimo tick PM | Celle oltre10ms | IRQ task | Ack HPET aggiunti |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 366 | 38402 | 1 | 263 | 39 |
| 2 | 344 | 118663 | 8 | 245 | 46 |
| 3 | 377 | 36992 | 1 | 268 | 40 |
| 4 | 359 | 29911 | 0 | 260 | 40 |
| 5 | 354 | 191050 | 1 | 255 | 41 |
| 6 | 354 | 48744 | 1 | 254 | 41 |
| 7 | 354 | 42313 | 1 | 254 | 41 |
| 8 | 332 | 109997 | 2 | 257 | 40 |
| 9 | 354 | 24493 | 0 | 255 | 41 |
| 10 | 354 | 61316 | 2 | 254 | 41 |

3548 celle, 17 oltre l'obiettivo; massimo 53,37 ms per flush. Il percorso
misurato comprende il controllo degli stack e la verifica completa del
renderer. Non abbiamo separato il costo delle fasi dal jitter dell'host.
Non si misura l'intero intervallo IF0 né la latenza end-to-end del tasto.
Il watchdog progredisce tra aggiornamenti: non interrompe un flush IF0.
Il delta modulo presuppone meno di un giro del contatore; la deadline VM
è un controllo separato, non una soluzione a un clock guasto.

## Prove host

Evidenza: `kernel/build/console-measured-host/` e relativo log.

- MSVC C11 /W4 /WX e linker /WX; ASan sulle unità C host.
- Rust: 4 casi cursore e 4 temporali, ciascuno debug/release, Clippy;
  policy temporale con 200000 campioni generati per esecuzione.
- Griglia: 374635 controlli; gateway: 2228226 scalar; inventario: 576 controlli;
  policy apertura HPET: 1100 controlli. Flusher e misuratore C passano.
- Snapshot diagnostico: baseline intatta su successo/fallimento; delta,
  budget, ambiente e overflow errati respinti, hardware mock.
- Mapper nativo: 554161 controlli, inclusi nuovi casi slot estraneo, owner
  parziale, lease duplicato, stage/pages/pin errati e backend non bound.
  CPU/PTE/accounting mock, non RAM fisica strumentata.
- Wrapper: 6 test active e 5 firmware. La campagna reale confronta anche
  valori temporali, perdita zero, progresso e 128 pixel fisici/visibili.
- Cppcheck: 13 unità di base e 13 active; zero error/warning/performance/
  portability. I conteggi style/information sono riportati sotto.

Conteggi cppcheck esatti: base 11 style e 18 information; active 26 style
e 26 information. Comprendono funzioni ABI che il sottoinsieme analizzato
non vede chiamate e include di sistema mancanti nel modello cppcheck;
non sono un audit statico pulito dell'intero OS. XML conservati.
Valgrind non disponibile in questo ambiente Windows, non eseguito.
ASan non strumenta Rust/Assembly né misura leak fisici. Nessuna allocazione
dinamica nuova nel misuratore; accounting fisico verificato separatamente.
Coverage linee/branch N/D.

## Immagini e campagne finali

Rendering con input: 10/10 boot consecutivi, q35/TCG, CPU max, BSP,
128 MiB, QEMU/OVMF. Ogni boot: 414 byte, 130 pressioni, 128 rilasci,
98 scalar, 10 comandi; 32 pressioni aggiuntive di stress, 38–45 idle.
Zero perdite osservate e budget di 20438 pagine prima/dopo la sessione.
Oracolo esterno: 128 pixel fisici e 128 visibili dell'ultima cella del ciclo A/è/€
per boot; il readback del renderer verifica anche le scritture intermedie.
Nessun fault inatteso. Sono prove finite, non stress continuo/SMP.

- Immagine `kernel/build/console-measured-native/kernel.elf`:
  `9BDE719EC868BB04D31DFA2A2EB15E0F59B507E37F814FB1B2BC56213AF5EFA3`.
- Campagna `kernel/build/ps2-input-firmware-a97798873284406f959888b3966379b6/`:
  `console-active-results.json`, manifest, seriali, trace, screenshot per boot.
- Regressione ordinaria 10/10 e senza video 10/10, immagine
  `kernel/build/console-measured-ordinary/kernel.elf`:
  `F3A17C2E5D5535133AD9D5DFB33EB44DBBF7B8CF4D38AA7EC03DB0E4967CDA02`.
- Evidenze regressioni:
  `kernel/build/framebuffer-live-fddb0149c34f41a88267d642680d8445/` e
  `kernel/build/framebuffer-live-b1a6cc0631ca4f1d97dbf06cbd4d7a52/`.
- Loader invariato:
  `EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7`.
- OVMF `33090CC07675BAA5190D9F1E84BF5176B33BCBFA9BACAC522961150CDB6DBB2A`.
- QEMU `47D57A6072E0BB3BD98F87926EB129EB1736DFE818C67B3B81EF7CE4EDD0B3CD`.

## Esito e seguito

Gate funzionale del collaudo superato con limite di 100 ms; obiettivo di 10 ms
non raggiunto, nessuna dichiarazione production-ready. Prima di aumentare
il lavoro video: profilare le fasi, ridurre il costo mantenendo l'autorità
sulle risorse e ripetere input/tempi/perdite. Output shell grafico ancora
da collegare. Review vincoli in `kernel/build/console-render-review.json`;
massimo 293 righe per sorgente nel perimetro, massimo 5 file diretti.
