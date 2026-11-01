#                                   VoidOS — Test

## Cos'è?

Questa cartella raccoglie test unitari, test di integrazione e diagnostici.
I percorsi rispecchiano i moduli sorgenti, così è possibile trovare le prove
accanto alla stessa suddivisione funzionale del progetto.

# #=======================================================================================================================================# #

## Struttura

| Cartella | Contenuto |
|---|---|
| [kernel/](kernel/) | Memoria, scheduler, servizi e driver comuni. |
| [arch/](arch/) | Test dei backend hardware; `arch/x86/` include boot UEFI e CPU x86. |
| [lib/](lib/) | Test delle librerie condivise. |
| [scripts/](scripts/) | Test degli strumenti comuni, tra cui la lettura dei log. |
| [tooling/](tooling/) | Verifiche degli strumenti e dei runner di test. |

Esempio: i test di `kernel/drivers/input/ps2/` si trovano in
`test/kernel/drivers/input/ps2/`. I diagnostici selezionati dalle immagini
kernel appartengono a questa struttura, anche quando vengono collegati nel guest.

# #=======================================================================================================================================# #

## Esecuzione

Dalla radice del repository, in PowerShell:

```powershell
./scripts/test/run.ps1 -List
./scripts/test/run.ps1 -Suite console
./scripts/test/run.ps1 -Suite all
./scripts/test/qemu.ps1 -List
```

Le suite host girano in processi PowerShell 7 separati. Il runner continua
dopo un errore e restituisce un esito finale negativo se una suite fallisce.
Ogni suite ha una deadline host di dieci minuti. Log ed esiti vengono
salvati in `kernel/build/test-run-*`, con stdout e stderr separati.

Le campagne QEMU sono distinte: un PASS host non dimostra un boot riuscito.
Alcune prove CPU richiedono immagini o pacchetti preparati e non appartengono
al comando host `all`. MSVC, Windows SDK, Rust e gli strumenti specifici
restano prerequisiti; per le campagne servono anche QEMU e il firmware previsto.

# #=======================================================================================================================================# #

## Convenzioni

I test sono qui; gli script specifici restano nelle rispettive cartelle
`scripts/`. Gli artefatti vanno nelle directory `build/`, non tra i sorgenti.
Le suite mantengono criteri distinti per casi positivi, fault intenzionali,
ownership e cleanup. La migrazione alla struttura centralizzata è descritta
nel [report dedicato](../docs/review/test-layout/validation.md).
