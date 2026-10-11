# Centralizzazione dei test — 10 ottobre 2026

## Modifica

Spostati 220 file dai precedenti `test/` locali in `test/kernel/`,
`test/arch/x86/` e `test/lib/`, mantenendo la corrispondenza con i moduli.
Include C, attributi Rust `#[path]`, build e comandi documentati aggiornati.
Nessuna modifica a `reuse/`.

Il test UEFI `kernel_file_test.c`, gia di 420 righe, separa ora il fixture
privato in `file_fixture.h`. Il confronto delle righe ricostruite verifica
che fixture e casi conservino tutte le righe originali. Lo script firmware
delle eccezioni e in `descriptors/scripts/firmware.ps1`, portando la cartella
dei descrittori a cinque sorgenti diretti.

Verificata l'identita del codice nei 24 sorgenti di produzione coinvolti:
le differenze riguardano esclusivamente `#include` e `#[path]` dei test.
Le due righe vuote aggiunte dall'utente in `batch/render.c` sono conservate;
eliminato soltanto lo spazio finale su una di esse.

Il runner centrale `scripts/test/run.ps1` esegue gli script esistenti in
processi separati, salva il risultato per suite e interrompe al primo errore.
La verifica pre-commit dell'11 ottobre ha riprodotto un blocco nell'attesa
Start-Process -Wait dopo la fine del processo figlio. Il runner attende ora
direttamente il figlio con WaitForExit; acquisisce prima Handle per conservare
ExitCode anche con Windows PowerShell 5.1, e rilascia il Process in finally.
La regressione su uscita0/7, stderr e percorsi con spazi passa su PowerShell
5.1 e7: `kernel/build/precommit-runner-winps51.log` e
`kernel/build/precommit-runner-regression.log`.
Il tentativo bloccato e conservato separatamente e non e un gate passato:
`kernel/build/test-run-3c4782d6818942e1b4f9ef208c749554/interrupted.txt`.
Il controllo `scripts/test/layout.py` verifica struttura e import relativi.
I controlli di modulo includono anche le nuove cartelle dei test.

## Prove host

Dodici suite passate: boot UEFI, kernel, bootstrap, scheduler comune,
scheduler x86, paging, PS/2, framebuffer comune, framebuffer x86, console,
descrizione HPET e watchdog. Eseguiti i controlli MSVC/MASM, Rust, Clippy,
ASan e cppcheck previsti dai rispettivi runner. I diagnostici
style/information non sono presentati come errori di compilazione.

Evidenza aggregata con i singoli log: `kernel/build/test-layout/validation.json`.
Verifica pre-commit dell'11 ottobre:12/12 suite nuovamente passate,
`kernel/build/test-run-0d6e491346cf4399bd4626957ca4c3c2/results.json`.
Le regressioni del runner definitivo passano su entrambe le versioni
PowerShell; il cache di Handle riguarda la lettura dell'esito del figlio,
senza modificare test C/Rust o immagini del kernel.
I tentativi falliti durante la migrazione restano nei propri log e non sono
conteggiati come successi. Gli errori di compilazione erano riferimenti nei
mock, nelle liste dinamiche o nei template interpolati, corretti prima
delle prove finali.

Il runner LAPIC ASan trasferito e passato anche separatamente:
10 ripetizioni e 10.000 cicli eventi; log `kernel/build/test-layout/lapic-asan.log`.

Layout finale: 221 sorgenti/script di test, massimo 395 righe,
massimo cinque sorgenti diretti per cartella; 67 import relativi verificati.
Parser PowerShell: 33 script controllati. Review di console, PS/2 e
framebuffer passate; `git diff --check` senza errori.

## QEMU sullo stato finale

| Profilo | Esito | Cartella delle evidenze in `kernel/build/` |
| --- | --- | --- |
| Kernel ordinario con framebuffer | 10/10 | `framebuffer-live-3fce662f11154cd9b6e19687e716a53c/` |
| Kernel ordinario senza video | 10/10 | `framebuffer-live-6f4c241a4194400385ffdee586b8c4c1/` |
| Input, shell e console a gruppi sotto IRQ | 10/10 | `ps2-input-firmware-809e9a7024014981bdb7cdc98dc26bcc/` |

Trenta boot positivi totali, dieci consecutivi per ciascun profilo, con
marker, deadline host e controllo delle VM possedute. Il loader resta
invariato durante ciascuna campagna. Il profilo input controlla il
ripristino del budget fisico e l'assenza di perdite; il renderer controlla
i pixel fisici e visibili tramite l'oracolo gia esistente.

SHA-256 delle immagini verificate:

- Ordinaria: `5E6BED3ED1608EFD105715CE0ADD27DBE590678F464281FEA289B13243C7328A`.
- Console a gruppi: `69F657C6FAE7DA63CA672A72CF074034096324657A96826BA2AB5076A620559F`.
- Loader: `FE6C2A456BCCA2D11DDE2C60A6FE9A48B513BFC8E4F0020EE56AF592B17D6873`.

Un tentativo iniziale usava il loader nel profilo diagnostico predefinito:
il log indicava cleanup senza avvio kernel, non un panic. Non e conteggiato.
Il loader e stato ricompilato con `-BootKernel` prima delle campagne positive.
Un primo comando console usava `--image` invece di `--kernel`: il parser lo
ha respinto senza avviare QEMU; l'esempio nel README e stato corretto.

## Limiti

Copertura linee/branch N/D. Valgrind non disponibile sul profilo Windows;
ASan strumenta i test C host previsti, non dimostra assenza generale di leak
RAM fisica e non strumenta Rust/Assembly in QEMU. L'accounting fisico e una
prova distinta nel diagnostico. Nessuna prova su hardware reale, SMP o
nuovo contratto hardware e stata introdotta da questa riorganizzazione.

I backup della migrazione sono in `kernel/build/test-layout/backup/`;
gli artefatti restano esclusi dal versionamento. Le modifiche sono nella
working tree: questa task non esegue commit o push.
