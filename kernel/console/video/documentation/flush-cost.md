# Costo dei flush della console video — 16 ottobre 2026

Richiesta utente: movimento del puntatore poco fluido in QEMU. Questa pagina
raccoglie le misure fatte per capire dove va il tempo; non dichiara una
fluidita raggiunta.

## Strumentazione

`x86_console_flush` misura in tick PM (3,579545 MHz) ogni flush del worker video e
li riporta alla chiusura: `KERNEL_CONSOLE_FLUSHES`, `..._FLUSH_TOTAL_PM`,
`..._FLUSH_MAX_PM`, `..._IDLE_FLUSHES`/`..._IDLE_TOTAL_PM` (flush senza celle da
disegnare) e `..._SETTLE_TOTAL_PM` (tempo nel record seriale del puntatore).
Sono tempi reali del guest in QEMU/TCG, con rumore dell'host: confrontare le
campagne fra loro, non con hardware fisico.

## Misure (profilo `-Ps2Input -Ps2Mouse -ConsoleShell`, harness con 651 eventi mouse)

| Immagine | Flush totali per boot | Media flush con lavoro | Massimo flush |
|---|---|---|---|
| Prima (3 boot) | 5,0–5,4 s | 2,95–3,43 ms | 18–28 ms |
| Scansione dirty a byte (10 boot) | 4,2–4,9 s (un boot 5,5 s) | 2,4–2,9 ms | 9–14 ms (un boot 23 ms) |

Altre misure della stessa campagna:

- Il record seriale del puntatore (`KERNEL_POINTER_FRAME_*`, circa 170 caratteri
  per frame) vale il 13–14,5 % del tempo di flush.
- Un ISR del mouse costa in media 0,043 ms (1300 chiamate, 56 ms in totale):
  non e il collo di bottiglia.
- Una chiamata a `kernel_console_peek` costa circa 2 us con la scansione a byte.
  Non spiega i circa 1–3 ms osservati anche nei flush senza lavoro: la causa
  principale resta non identificata.

## Cambiamenti tenuti

- Scansione della mappa dirty a byte invece che a cella
  (`kernel/console/video/console.c`), con test di equivalenza rispetto alla
  definizione per cella su nove dimensioni di griglia, avvii non allineati e bit
  spuri oltre la fine (`test/kernel/console/video/host/cases.c`).
- Contatori di tempo dei flush.

## Esperimenti ritirati

Patch in `kernel/build/phase4-experiments.patch` (non versionata). Il record seriale del puntatore
emesso solo a riposo (50 ms senza movimenti) e la stampa degli eventi mouse limitata ai
primi 64 hanno ridotto il lavoro seriale, ma richiedevano che il worker restasse in
polling a 1 ms finche il record era dovuto: i flush vuoti sono saliti da circa 80 a
circa 1750 per boot e il costo totale non e diminuito. Hanno inoltre reso intermittenti
due controlli dell'harness (QEMU puo fondere due `mouse_move` in un solo pacchetto, e
un record dopo `echo exit` non arrivava). Ritirati senza aver dimostrato un guadagno.

## Ipotesi non verificate

- La finestra SDL di QEMU aggiorna lo schermo a circa 30 ms per frame (circa 33 Hz):
  un cursore software disegnato dal guest non puo apparire piu fluido di cosi,
  qualunque sia il costo del flush. Un cursore hardware (virtio-gpu o vmware-svga)
  o un puntatore assoluto gestito dall'host eviterebbe il limite, ma richiede un driver
  nuovo e una scelta di design.
- Il costo fisso di circa 2,5 ms per flush con lavoro viene in parte dal controllo
  completo del paging per gruppo (94 % del tempo nella profilazione del 10 ottobre,
  [profile-validation.md](profile-validation.md)); ridurlo richiede di rivedere il
  contratto di ownership, non una micro-ottimizzazione.

## Prove

Suite host `console` (C/ASan, Rust, cppcheck) e 10 boot QEMU consecutivi con mouse,
puntatore, testo e shell; budget fisico 20415 ripristinato in tutti i boot; zero
perdite mouse. Immagine SHA256
`86BFF79BCB9FDB6BEB546744A6D438C4965579AAF66DA6C51CA7A3A004DC8C2B`;
evidenza `kernel/build/ps2-input-firmware-8ed982aa1f0f400c805b40ba0fc7a528`.
