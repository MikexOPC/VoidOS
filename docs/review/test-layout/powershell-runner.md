# Correzione runner PowerShell — 10 ottobre 2026

## Causa

Il runner originario sceglieva l'interprete tramite il processo chiamante
e invocava le suite come comandi nativi con `*> log`. In Windows PowerShell
5.1, stderr puo diventare `NativeCommandError`; con preferenza `Stop`
interrompe l'esecuzione prima del controllo di `$LASTEXITCODE`. Le suite
erano state collaudate con PowerShell 7: la compatibilita del runner con
il terminale 5.1 non era stata verificata nella migrazione iniziale.
Anche il runner bootstrap redirige l'XML di cppcheck su stderr.

## Correzione

`scripts/test/run.ps1` risolve PowerShell 7 dal PATH, dalla directory
standard oppure dal runtime locale di Codex. Il terminale chiamante puo
restare PowerShell 5.1; i runner di modulo continuano a usare la versione
gia verificata. Se il runtime manca, l'esecuzione termina con un messaggio
esplicito, senza ripiegare sulle semantiche problematiche di 5.1.

Ogni suite viene avviata con processo nascosto, stdout/stderr in file
separati, attesa del termine e verifica del codice reale di uscita.
`results.json` contiene anche `ErrorLog`. Il processo viene rilasciato dopo
aver acquisito l'esito. Stderr non viene ignorato e un errore reale non
viene convertito in successo. Non sono state cambiate le suite native.

## Prove

- Windows PowerShell 5.1: bootstrap reale passato tramite `-File`.
- Windows PowerShell 5.1: stessa chiamata con dot-sourcing dell'utente,
  bootstrap reale passato anche sulla versione finale del runner.
- `test/tooling/runner/process.ps1`: stdout/stderr conservati, codice zero
  accettato e codice 7 mantenuto; percorso con spazi e risoluzione del
  runtime senza PATH verificati da PowerShell 5.1.
- Sequenza host completa: 12/12 suite passate; evidenze in
  `kernel/build/test-run-5dbbc489bca14c77953fd7ea17a61d87/results.json`.
- Diff senza errori di whitespace; sintassi e layout verificati.

Il primo tentativo di avviare il test 5.1 tramite `-File` e stato respinto
dalla policy locale. Il collaudo ha usato `-ExecutionPolicy Bypass` soltanto
sul processo di test, senza cambiare la policy di sistema. Il runner non
modifica execution policy.

QEMU, sanitizer kernel e leak RAM fisica: N/A per questa correzione della
sola orchestrazione. Le suite host richiamate conservano i propri gate;
nessuna nuova prova hardware e dichiarata. Le campagne QEMU della precedente
migrazione restano evidenze di quel checkpoint.
