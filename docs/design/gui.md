# Desktop e applicazioni di VoidOS

## Obiettivo e scelta approvata

Sfondo lavanda e dock aprono le app presenti sul disco. L'utente ha scelto
l'opzione A il 23 novembre 2026. Il compositor `/bin/ui.elf` avvia e gestisce
i processi applicativi, conserva i canali IPC e compone le superfici SHM.
`gui` apre il desktop dalla shell dopo l'inizializzazione PS/2.

## Task

1. Terminale reale dal dock, input, lettura VFS, chiusura e recupero risorse.
2. System Monitor con dati reali, due app aperte e gestione del focus.
3. Scenario completo, stress, verifica dell'immagine e aggiornamento dello stato.

Ogni task termina con verifiche e commit locale. Lo stato è nella
[validazione GUI](../validation/gui.md).

## Ownership e stati

- Il compositor possiede display, heap e backbuffer. Le app ricevono un
  budget ristretto con SPAWN e DISPLAY revocati.
- Lo spawn con connessione prepara processo ed endpoint prima di pubblicare
  il figlio come eseguibile. Il bit 63 dell'argomento iniziale distingue
  l'endpoint IPC dal modo self-check `1`; i restanti bit contengono l'handle.
- Il creatore mappa la propria SHM in lettura e scrittura. Può pubblicare
  una superficie al possessore esclusivo del display, che la mappa in sola
  lettura. Il pin temporaneo protegge il backing durante la creazione delle
  PTE. Il fallimento annulla le risorse parziali e permette il retry.
- Le viste restituiscono indirizzo e dimensione. Il compositor valida tipo,
  dimensioni e rettangoli prima di leggere. Le PTE conservano U/S e NX.
- Le finestre usano un vettore dinamico. Il catalogo comprende gli eseguibili
  attuali. Un clic sull'app già avviata la porta in primo piano.
- La chiusura ferma un figlio del chiamante, raccoglie il processo, chiude
  l'endpoint e rilascia la vista. Un errore IPC termina la sola app interessata.
  Il kernel raccoglie i discendenti quando il padre viene raccolto, anche
  dopo un fault. La console riprende dopo la chiusura del desktop.

Il dispatcher BSP alterna compositor, figli e input. Avvio e operazioni SHM
sono riservati al BSP perché il backend di lease e paging GUI ha un unico
owner. Gli AP continuano il proprio dispatcher e i percorsi IPC già esistenti.

## ABI e tempo

`SYS_SPAWN` conserva la struttura esistente. `Flags=1` interpreta `InitialArg`
come puntatore al risultato `{pid, endpoint}` del padre. Con `Flags=0` resta
l'argomento ordinario del figlio. I flag sconosciuti vengono rifiutati.

| Numero | Operazione |
| --- | --- |
| 23 | Pubblicazione SHM al possessore del display |
| 24 | Vista SHM con indirizzo e dimensione |
| 25 | Arresto di un figlio del chiamante |
| 26 | Chiusura di un endpoint IPC del chiamante |

Le copie protette delle syscall ammettono anche la finestra SHM
`[0x10000000, 0x40000000)`, con recupero dei fault e rispetto dei permessi.
L'uptime parte dalla prima richiesta di informazioni display del kernel.
Il contatore ACPI PM viene accumulato oltre i wrap e campionato anche dal
timer BSP. Gli AP leggono lo snapshot pubblicato con atomiche. Un errore
della sorgente viene restituito al chiamante.

## Rendering e capacità attuali

Slint `no_std` usa il renderer software. Le superfici delle app diventano
immagini Slint. Il tema comprende sfondo lavanda, cornici traslucide, ombre,
dock animato e cursore. Terminale mantiene i comandi interni `help`, `print`,
`fl`, `rp`, `clear`, `exit`, con accesso VFS reale. Le sue celle sono 56×15.
Le percentuali dimostrative di System Monitor vengono eliminate nella task 2.

Input PS/2 e rendering CPU sono i backend attuali. L'heap Slint resta di
4 MiB. L'hardware fisico deve ancora essere verificato.

La crate UI e la nuova crate Cargo di `libui` dichiarano `GPL-3.0-only`,
coerentemente con la licenza del repository. Il backend usa le API pubbliche
`Image` e `SharedPixelBuffer` di Slint. I permessi R/W seguono Intel SDM,
Volume 3A, dicembre 2024, §5.6.1.

[Slint Image](https://docs.slint.dev/latest/docs/rust/slint/struct.Image),
[Intel SDM](https://cdrdv2-public.intel.com/843856/253668-sdm-vol-3a-dec-24.pdf).
