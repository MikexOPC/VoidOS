# Prova manuale del cursore in QEMU

Da PowerShell:

```powershell
Set-Location D:\OS
powershell -NoProfile -ExecutionPolicy Bypass -File .\kernel\console\video\scripts\pointer\run.ps1
```

Lo script usa l'immagine cursor-transaction-final e crea una ESP isolata
con copia identica di loader/kernel, variabili OVMF nuove e log seriale.
Il manifest `session.json` registra argomenti, percorsi e hash. I file della
sessione restano in `kernel/build/pointer-interactive-<id>/`.

Per ricompilare il profilo dalle sorgenti correnti:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\kernel\console\video\scripts\pointer\run.ps1 -Rebuild
```

La ricompilazione non certifica automaticamente sorgenti cambiate dopo
il collaudo: i risultati del [report](pointer-validation.md) valgono per il suo hash.

## Uso

1. Attendi il prompt della console e il cursore bianco con contorno nero.
2. Clicca nella finestra QEMU per catturare il mouse; muovilo anche ai bordi.
3. Prova `help`, `echo ciao` e `status`; verifica che testo e cursore non lascino residui.
4. Ctrl+Alt+G alterna la cattura di mouse/tastiera, come nella
   [documentazione ufficiale QEMU](https://www.qemu.org/docs/master/system/keys.html).
5. `exit` oppure Esc termina la sessione input con cleanup; poi chiudi la finestra.

Il profilo e diagnostico e limitato:65536 passi dispatcher e32768 attese idle.
L'attesa input usa HPET a5ms, invece dei100ms del precedente profilo.
Non e ancora una sessione desktop permanente; per il primo test fai una prova
breve. I pulsanti mouse vengono decodificati, senza azioni GUI sulle finestre.
Non si aggiunge un tablet USB: il driver corrente usa il mouse PS/2 relativo.

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
