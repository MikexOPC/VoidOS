# Prova manuale del cursore in QEMU

Da PowerShell:

```powershell
Set-Location D:\OS
powershell -NoProfile -ExecutionPolicy Bypass -File .\kernel\console\video\scripts\pointer\run.ps1
```

Lo script ricompila il profilo interattivo dalle sorgenti correnti in
`kernel/build/pointer-interactive-image/` e verifica il MAP prima di avviare QEMU:
probe diagnostici e oggetti test devono essere assenti. Crea quindi una ESP isolata
con copia identica di loader/kernel, variabili OVMF nuove e log seriale.
Il manifest `session.json` registra argomenti, percorsi e hash. I file della
sessione restano in `kernel/build/pointer-interactive-<id>/`.

Anche `-Rebuild` forza questa compilazione. Un percorso esplicito passato con
`-Image` seleziona invece il binario gia costruito, senza ricompilarlo:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\kernel\console\video\scripts\pointer\run.ps1 -Rebuild
```

La ricompilazione non certifica automaticamente sorgenti cambiate dopo
il collaudo: i risultati del [checkpoint grafico](pointer-presentation.md) valgono per il suo hash.

## Uso

1. Attendi il prompt della console e il cursore nero con contorno bianco del SVG fornito.
2. Clicca nella finestra QEMU per catturare il mouse; muovilo anche ai bordi.
3. Prova `help`, `echo ciao` e `status`; verifica che testo e cursore non lascino residui.
4. Ctrl+Alt+G alterna la cattura di mouse/tastiera, come nella
   [documentazione ufficiale QEMU](https://www.qemu.org/docs/master/system/keys.html).
5. Esc annulla la riga digitata: puoi continuare a scrivere e muovere il mouse.
6. Il comando `exit` termina la sessione input con cleanup; poi chiudi la finestra.

Il profilo input non termina piu al vecchio limite di passi/attese (B1).
Le singole attese restano limitate e sorvegliate dal watchdog; l'attesa input usa
HPET a 5 ms. Non e ancora una sessione desktop o una GUI. I pulsanti mouse vengono decodificati, senza azioni GUI sulle finestre.
Non si aggiunge un tablet USB: il driver corrente usa il mouse PS/2 relativo.
Le tracce per evento e per frame restano nel profilo diagnostico; il profilo
interattivo conserva errori e contatori finali. Le copie sul framebuffer non
sono sincronizzate al vblank: verificare manualmente fluidita e artefatti.

## Esc aggiornato — 19 ottobre 2026

La sessione resta attiva dopo Esc; il testo annullato non viene inviato alla shell.
Per verificarlo: digita exit senza Enter, premi Esc, esegui help, muovi il mouse,
poi digita un nuovo exit e premi Enter per il cleanup.
Immagine e prove aggiornate nel [contratto input](../../../../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md).
Le prove seguenti restano il checkpoint precedente, quando Esc richiedeva lo stop.

## Consolidamento del launcher — 19 ottobre 2026

Il percorso predefinito non usa piu il vecchio `pointer-dirty-interactive`.
`launcher_test.ps1` verifica la build corrente, gli hash delle copie, ESP/OVMF isolate,
profilo SDL e rifiuto delle immagini errate. I messaggi di compilazione non entrano
nell'oggetto di preparazione restituito da `-PrepareOnly`.

Immagine finale SHA256 `758206602CEE9E5B7ABAA828CA195036810CD56496B200ADB05B4EA6C13A3507`;
loader SHA256 `3FD5B06E8891A16176FFCE1B197607EA31841B5EF9B231B33D3E4254D3FC9FAA`.
Audit MAP: 2.117 simboli, probe vietati e oggetti test assenti.

Dieci boot interattivi passano: 6.000 comandi di movimento, 120 scene assestate
confrontate pixel per pixel, 324 pixel fisici finali verificati per boot, help, Esc
e cleanup. Budget fisico ripristinato in ogni boot. Evidenze:
`kernel/build/pointer-interactive-check-a94ea60ef9a549d2b0ef8b1cb8f802f7/`.

Controllo aggiuntivo delle catture durante le transazioni: 188 complete, 5 parziali,
nessuna assente (`shape-audit.json`). Tutte le catture parziali hanno una successiva
cattura corretta nello stesso segmento; le 120 scene assestate sono esatte
(`settled-audit.json`). Il front buffer viene aggiornato progressivamente: questa
prova non certifica frame atomici, assenza di artefatti transitori o fluidita SDL.

L'ESP preparata dal launcher passa movimento, help, Esc e cleanup:
`kernel/build/pointer-interactive-ca8ecc104954495a816eb780a97abaca/smoke-results.json`;
log `kernel/build/consolidation-launcher-test.log` e
`kernel/build/consolidation-launcher-smoke.log`.

## Checkpoint dello script prima della correzione lag — 12 ottobre 2026

`test/kernel/console/video/pointer/launcher_test.ps1`: preparazione isolata,
hash kernel/loader, profilo SDL/CPU e rifiuto di immagine mancante o senza
puntatore. Evidenza `kernel/build/pointer-launcher-test.log`.

`launcher_smoke.py` avvia l'ESP preparata sostituendo soltanto il display SDL
con none e il monitor con stdio: movimento17,9, help, Esc, KERNEL_READY,
cleanup pointer/console/PS2/mouse/scheduler verificati. Evidenza
`kernel/build/pointer-launcher-smoke.log` e
`kernel/build/pointer-interactive-4858e8e7c6f3438a85805d4ab7959967/smoke-results.json`.
Nessuna prova automatica di interazione umana con la finestra SDL dichiarata.

Il binario QEMU installato conferma SDL e `show-cursor=off` nel suo help.
Queste prove precedevano la segnalazione manuale di perdita e panic.
Per le correzioni e le nuove prove vedere [lag e input continuo](pointer-latency.md).
La successiva segnalazione manuale e il consumo a raffiche con la nuova
freccia antialias sono descritti in [puntatore aggiornato](pointer-burst.md).
Il save/restore corrente e verificato nel [report transazione](cursor-transaction-validation.md).
