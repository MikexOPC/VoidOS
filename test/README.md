# Test del progetto

I test sono raccolti qui e rispecchiano i percorsi dei sorgenti:

- `test/kernel/`: core, memoria, scheduler, servizi e driver comuni.
- `test/arch/x86/`: boot UEFI e backend specifici x86.
- `test/lib/`: librerie condivise.

I test diagnostici collegati nelle immagini kernel sono inclusi in questa
struttura; gli script di build ne selezionano i nuovi percorsi. Le suite non
sono state unite e mantengono i propri criteri di accettazione.

Prove della migrazione: [report di validazione](../docs/review/test-layout/validation.md).

## Esecuzione

Dalla radice del repository, in PowerShell:

```powershell
./scripts/test/run.ps1 -List
./scripts/test/run.ps1 -Suite console
./scripts/test/run.ps1 -Suite all
```

Il runner richiama gli script esistenti in processi separati, conserva i log
in `kernel/build/test-run-*` e interrompe la sequenza al primo errore.
Il terminale puo essere Windows PowerShell 5.1: le suite vengono eseguite
con PowerShell 7, cercato nel PATH, nell'installazione standard o nel runtime
locale di Codex. Se nessuno e disponibile, il runner lo segnala. Stdout e stderr sono
salvati separatamente; il successo richiede codice di uscita zero, non
l'assenza di testo su stderr. Il runner attende direttamente il processo
della suite con WaitForExit, senza l'attesa dell'intero albero di processi
di Start-Process -Wait. Con PowerShell 5.1 l'XML di cppcheck su stderr
puo essere convertito in `NativeCommandError`, anche senza un errore di analisi.
MSVC, Windows SDK, Rust e gli strumenti richiesti dalle singole suite restano
prerequisiti. La suite `boot` usa il builder UEFI, che esegue anche i test host
del bootloader e rigenera gli artefatti EFI/ESP in `arch/x86/boot/uefi/build/`.
Non avvia QEMU; usa il profilo diagnostico predefinito del builder.

Le campagne QEMU restano esplicite: il successo delle suite host non dimostra
dieci boot superati. Esempio per la console diagnostica a gruppi:

```powershell
./arch/x86/boot/uefi/scripts/build.ps1 -BootKernel
./kernel/scripts/build.ps1 -TestConsoleBatch -OutputDirectory D:/OS/kernel/build/console-batch
python ./kernel/console/video/scripts/active.py --kernel D:/OS/kernel/build/console-batch/kernel.elf --boots 10 --draw
```

Gli script del componente restano in `scripts/`; quelli gia associati a un
gruppo di test restano nella sua sottocartella `scripts/`. Gli artefatti non
vanno in `test/`. `reuse/` non partecipa alla migrazione.
