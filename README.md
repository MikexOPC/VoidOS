# VoidOS

VoidOS è un sistema operativo sperimentale con un bootloader UEFI proprio
e un kernel progettato per evolvere verso un'architettura ibrida.
L'obiettivo è costruire una base sicura e modulare, fino a supportare
applicazioni isolate e un'interfaccia grafica.

Il progetto usa principalmente C, con Rust per validazioni e gestione
degli stati e Assembly per le operazioni a basso livello.

## Stato del progetto

La piattaforma attuale è x86-64, sviluppata su Windows e verificata con
QEMU/OVMF. Sono presenti boot UEFI, paging, gestione della memoria,
scheduler cooperativo sul BSP, input PS/2, shell e console grafica.
L'avvio degli altri core è in fase di collaudo: non è ancora disponibile
uno scheduler SMP operativo. Userspace, syscall e filesystem del kernel
restano obiettivi futuri.

Per i risultati verificati e i limiti attuali consultare [docs/status.md](docs/status.md).

## Struttura

| Cartella | Contenuto |
|---|---|
| `arch/` | Codice specifico della piattaforma; in `arch/x86/` si trovano bootloader UEFI, backend del kernel, paging e driver x86. |
| `kernel/` | Servizi del kernel, memoria, scheduler, console, shell e driver comuni. |
| `lib/` | Librerie e funzioni riutilizzabili. |
| `test/` | Test C, Rust e di integrazione, organizzati secondo i moduli sorgenti. |
| `scripts/` | Strumenti comuni di compilazione, test e gestione delle campagne QEMU. |
| `docs/` | Stato del progetto, architettura, istruzioni e risultati di validazione. |

Gli script specifici dei moduli si trovano nelle rispettive sottocartelle
`scripts/`. Gli artefatti generati nelle directory `build/` sono esclusi
dal versionamento.

## Licenza

VoidOS è distribuito sotto la **GNU General Public License, versione 3**
(GPL-3.0-only). Il testo completo è disponibile in [LICENSE](LICENSE).
Eventuali componenti di terze parti conservano le proprie licenze.
