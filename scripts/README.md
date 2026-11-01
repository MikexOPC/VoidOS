#                                   VoidOS — Script

## Cos'è?

Questa cartella contiene gli strumenti comuni di compilazione, test e
manutenzione. Gli script specifici di un componente restano nella sua
sottocartella `scripts/`, per esempio `kernel/scripts/` per il kernel.

# #=======================================================================================================================================# #

## Struttura

| Percorso | Contenuto |
|---|---|
| `lib/` | Helper condivisi per toolchain, layout, log seriali e controllo delle VM QEMU. |
| `test/` | Runner delle suite host, campagne QEMU e registro `campaigns.json`. |
| `format/` | Strumenti di formattazione dei sorgenti. |
| [clean.ps1](clean.ps1) | Pulizia controllata delle directory di esecuzione in `kernel/build/`. |

`lib/common.ps1` raccoglie helper MSVC e analisi statica.
`lib/serial.py` interpreta marker e campi dei log; `lib/qemu.py` gestisce
VM possedute dal test, ESP isolate, monitor e risultati delle campagne.

# #=======================================================================================================================================# #

## Comandi

Eseguire dalla radice del repository, in PowerShell:

```powershell
./scripts/test/run.ps1 -List
./scripts/test/run.ps1 -Suite all
./scripts/test/qemu.ps1 -List
./scripts/test/qemu.ps1 -Campaign v2c,fallback
./scripts/test/qemu.ps1 -Smoke
./scripts/clean.ps1
```

`run.ps1` esegue le suite host registrate, continua dopo i fallimenti e
produce un riepilogo finale. Le campagne QEMU sono separate: `qemu.ps1`
compila le immagini e lancia i runner indicati nel registro. `-Smoke`
esegue un boot per campagna; è il controllo previsto prima dei commit
che toccano il kernel, non una campagna completa di affidabilità.

`qemu.ps1` richiede PowerShell 7; il runner host lo cerca per le suite.
Gli script di build dipendono dalla toolchain prevista dal componente.
Per dettagli sui test vedere [test/README.md](../test/README.md).

# #=======================================================================================================================================# #

## Artefatti e pulizia

I log e i risultati sono conservati nelle directory `build/` dei componenti.
`clean.ps1` mostra le directory candidate senza eliminarle; la rimozione
richiede `-Apply`. Mantiene le esecuzioni recenti e le evidenze riconosciute
come citate, senza eliminare indiscriminatamente l'intera cartella di build.

La cartella sorgente `kernel/scripts/build/` contiene helper di compilazione:
non è una directory di artefatti e deve rimanere versionata.
