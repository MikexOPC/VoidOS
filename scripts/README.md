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
| `tooling/build-plan/` | Planner Rust per confrontare tre profili di build del kernel. |
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
./scripts/test/run.ps1 -Suite build-plan
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

La suite `build-plan` compila lo strumento host Rust e confronta argomenti
e ordine dei comandi con `kernel/scripts/build.ps1 -PlanOnly`. Copre il
boot ordinario, il timer periodico BSP e il timer AP. Il confronto salva
i due piani in `kernel/build/` senza compilare o installare il kernel.
La build standard continua a usare PowerShell. Per compilare con il piano
Rust uno dei tre profili verificati, indicare una nuova directory di output.
La modalità sperimentale usa la stessa toolchain e lo stesso convertitore
PE/ELF della build standard. Pubblica `kernel.elf` dopo la conversione.

```powershell
./kernel/scripts/build.ps1 -UseRustPlan -OutputDirectory ./kernel/build/rust-pilot
./kernel/scripts/build.ps1 -UseRustPlan -TestSchedulerPeriodic -OutputDirectory ./kernel/build/rust-bsp
./kernel/scripts/build.ps1 -UseRustPlan -TestApService ap-timer -OutputDirectory ./kernel/build/rust-ap
./scripts/test/qemu.ps1 -Campaign rust-plan-ordinary,rust-plan-bsp-periodic,rust-plan-ap-timer
```

Il linker riceve un file di risposta UTF-8 con BOM per conservare
l'ordine degli oggetti e i caratteri nei percorsi.
[Riferimento MSVC](https://learn.microsoft.com/en-us/cpp/build/reference/unicode-support-in-the-compiler-and-linker?view=msvc-170)

`qemu.ps1` richiede PowerShell 7; il runner host lo cerca per le suite.
Gli script di build dipendono dalla toolchain prevista dal componente.
Per dettagli sui test vedere [test/README.md](../test/README.md).

Le campagne che usano `lib/qemu.py` mostrano, al termine delle verifiche,
un separatore per ciascun boot e il relativo esito.

```text
# ====== Boot 1 ====== #
[ Pass ]

# ====== Boot 2 ====== #
[ Fail ]
Causa: Missing KERNEL_READY
```

Solo `[ Pass ]` e `[ Fail ]` sono colorati, rispettivamente in verde e
rosso. I log su file restano testo semplice. Dopo un errore il riepilogo
indica quanti boot sono rimasti non eseguiti. La causa riporta l'errore
rilevato dal test; le evidenze e il traceback conservano i dettagli.
La suite host `runner` verifica esiti, colori, log e codici di uscita.

Verifica del 5 novembre 2026 per questo blocco di reporting. Sei test
Python, regressioni PowerShell della suite `runner` e controllo layout
passati. Tre boot ordinari consecutivi su QEMU/OVMF, quattro CPU e immagine
ricompilata, passati. Evidenze in
`kernel/build/qemu-run-9318ba92fcf049a6af9f6929f809c509` e
`kernel/build/ap-runtime-67cf571e0298416dab80e4725583ac72`.
Queste prove riguardano il reporting e il profilo ordinario esistente.
Il dispatcher sugli AP di K4-C4 richiede ancora implementazione e collaudo.

# #=======================================================================================================================================# #

## Artefatti e pulizia

I log e i risultati sono conservati nelle directory `build/` dei componenti.
`clean.ps1` mostra le directory candidate senza eliminarle; la rimozione
richiede `-Apply`. Mantiene le esecuzioni recenti e le evidenze riconosciute
come citate, senza eliminare indiscriminatamente l'intera cartella di build.

La cartella sorgente `kernel/scripts/build/` contiene helper di compilazione:
non è una directory di artefatti e deve rimanere versionata.
