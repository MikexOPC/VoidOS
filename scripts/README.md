# Script di build e test (G2)

24 ottobre 2026. Infrastruttura comune per i test, nata dai problemi visti in sessione: controlli
sui log per sottostringa (`..._TIMEOUT` trovava `..._TIMEOUTS`), campagne non eseguite per
settimane e quindi rimaste indietro rispetto al kernel, codice copiato in decine di script.

## Comandi

| Comando | Cosa fa |
|---|---|
| `scripts/test/run.ps1` | Tutte le suite host (32): continua dopo un errore, riepilogo finale, rifiuta un `test.ps1` non elencato |
| `scripts/test/qemu.ps1 -List` | Elenca le campagne QEMU registrate in `campaigns.json` |
| `scripts/test/qemu.ps1 -Campaign v2c,fallback` | Compila e lancia le campagne indicate, con il loro numero di boot |
| `scripts/test/qemu.ps1 -Smoke` | Un boot per ogni campagna (circa 15 minuti) |

**Regola (G2, R3-A):** lo smoke set gira prima di ogni commit che tocca il kernel.

## Librerie

- `scripts/lib/serial.py`:
  - legge il log seriale per eventi, dopo aver tolto i codici ANSI;
  - un marker è una riga intera `[ OK|INFO|WARN|FAIL ] NOME`, un campo è `NOME: 0x` seguito da 16
    cifre;
  - `has`, `count`, `before`, `number`, `failures`, `terminal`.
- `scripts/lib/qemu.py`:
  - `Vm` per un boot: ESP isolata con hash verificato, profili video `std`, `virtio` e `iommu`,
    `wait`, comandi del monitor, screenshot e lettura della memoria fisica;
  - chiude solo il processo che ha avviato;
  - `finish()` scrive un `results.json` con lo stesso schema per tutti e la riga
    `RESULT PASS|FAIL nome n/m`.
- `scripts/lib/common.ps1`:
  - `Invoke-Checked` e `Invoke-OsChecked`, con log opzionale e `-Tee`;
  - `Use-Msvc` e `Restore-Msvc`, `Invoke-Cppcheck`, `Read-Hex`.

## Stato della migrazione (R1-A, a tappe)

- **`test/kernel/console/video/pointer/interactive.py`:** usa `Vm` e `SerialLog`. Verificato su
  tutti i profili: V2c, fallback, stallo, plain, IOMMU assente.
- **31 script `test.ps1`:** usano `common.ps1` per `Invoke-Checked` (R2-A). Il build del loader
  (`arch/x86/boot/uefi/scripts/build.ps1`) ha una variante diversa ed è rimasto com'è.
- **Ambiente MSVC:** le 37 copie restano nei rispettivi script. `Use-Msvc` va adottato quando si
  tocca uno script, perché le varianti sono troppo diverse per una sostituzione meccanica sicura.
- **Altri runner QEMU** (`ps2/scripts/firmware.py`, `framebuffer/scripts/firmware.py`,
  `test/kernel/firmware.ps1` e gli altri): da migrare quando si toccano. Sono comunque tutti nel
  registro, quindi lo smoke set li esegue.

## Prove

| Prova | Esito | Evidenza (`kernel/build/`) |
|---|---|---|
| Test unitari di `serial.py` (ANSI, prefissi simili, ordine, numeri, file mancante) | 4/4 | `test/scripts/lib/serial_test.py` |
| `interactive.py` migrato: V2c ×2, stallo, fallback, plain, assente | tutti PASS | `pointer-interactive-check-55931eab…`, `…c04e86cb…`, `…82a752d5…`, `…57c17f24…`, `…85bb8e2b…` |
| `run.ps1` dopo la sostituzione di `Invoke-Checked` | 31/32 (virtio-discovery: `read.c` dell'utente) | `test-run-5716ad8a708e4b9b91a3869a17ba4723` |
| `qemu.ps1 -Smoke` | 10/10, compresa la campagna di boot del kernel (23/23 scenari) | `qemu-run-0f357ff2c98a47c6b8de926d86afc62c` |
