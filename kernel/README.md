#                                   VoidOS — Kernel

## Cos'è?

Questa cartella contiene i servizi del kernel e i driver comuni. Il codice
specifico x86 si trova in [arch/x86/kernel/](../arch/x86/kernel/).
Il punto d'ingresso principale è [main.c](main.c); la sequenza di avvio
è suddivisa tra `startup/` e `bootstrap/`.

# #=======================================================================================================================================# #

## Struttura

| Cartella | Contenuto |
|---|---|
| `boot/` | Contratto ricevuto dal loader e controlli sui dati di boot. |
| `bootstrap/` | Preparazione iniziale della memoria del kernel. |
| `startup/` | Coordinamento delle fasi di inizializzazione e dei profili di avvio. |
| `mm/` | Allocatore fisico, heap e inventario della memoria. |
| `sync/` | Lock, contesti di sincronizzazione e regole di acquisizione. |
| `scheduler/` | Gestione delle task, stati, attese ed eventi. |
| `time/` | Interfacce comuni per il tempo e le scadenze. |
| `firmware/` | Lettura e validazione delle informazioni firmware, tra cui ACPI. |
| `drivers/` | Driver comuni, organizzati per categoria e dispositivo. |
| `input/` | Eventi e trasformazioni dell'input, separati dai driver hardware. |
| `console/` | Console video, testo, editor di riga e puntatore. |
| `shell/` | Comandi della shell e coda delle richieste. |
| `diagnostics/` | Messaggi e strumenti diagnostici del kernel. |
| `scripts/` | Compilazione del kernel e selezione dei profili. |
| `documentation/` | Documentazione del punto d'ingresso e del coordinamento generale. |

Le directory `build/` contengono artefatti generati e non fanno parte dei sorgenti.

# #=======================================================================================================================================# #

## Dove cercare

I test corrispondenti sono in [test/kernel/](../test/kernel/).
C rimane nella cartella funzionale; Rust e Assembly sono raccolti nelle
sottocartelle del linguaggio e della funzione. Le operazioni privilegiate
specifiche della CPU appartengono ai backend in `arch/`.

Lo scheduler ordinario è ancora cooperativo sul BSP. Per distinguere
funzioni operative e diagnostici consultare [lo stato del progetto](../docs/status.md).
