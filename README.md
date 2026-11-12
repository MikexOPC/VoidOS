# VoidOS

## Cos'è?

VoidOS è un sistema operativo sperimentale con un bootloader UEFI proprio
e un kernel progettato per evolvere verso un'architettura ibrida.
L'obiettivo è costruire una base sicura e modulare, fino a supportare
applicazioni isolate e un'interfaccia grafica.

Il progetto usa principalmente C, con Rust per validazioni e gestione
degli stati e Assembly per le operazioni a basso livello.

---

## Stato del progetto

La piattaforma attuale è x86-64, sviluppata su Windows e verificata con
QEMU/OVMF. Sono presenti boot UEFI, paging, gestione della memoria,
scheduler cooperativo sul core di avvio (BSP), input PS/2, shell e console
grafica.

La shell ordinaria può eseguire un task del kernel alla volta sul primo
core secondario (AP1) con `ap <n>`. I driver restano sul BSP. I profili
diagnostici verificano anche la preemption su più core. Al boot il kernel
può eseguire un primo programma Rust in ring 3. Sono disponibili syscall
di base e una prova IPC tra due processi utente. Nel profilo shell PS/2,
`bridge status` interroga un servizio residente in ring 3. Il filesystem
del kernel resta un obiettivo futuro.

Per le prove e i limiti attuali, consulta lo [stato del progetto](docs/status.md).
Le istruzioni per compilare ed eseguire i test sono nella [guida agli script](scripts/README.md).

---

## Struttura

| Cartella | Contenuto |
|---|---|
| [`arch/`](arch/) | Codice specifico della piattaforma. `arch/x86/` contiene bootloader UEFI, backend del kernel, paging e driver x86. |
| [`kernel/`](kernel/) | Servizi del kernel, memoria, scheduler, console, shell e driver comuni. |
| [`userspace/`](userspace/) | Programma iniziale e struttura per librerie e servizi utente. |
| [`bridge/`](bridge/) | Progetto dell'API e del servizio che media le richieste delle applicazioni. |
| [`lib/`](lib/) | Librerie e funzioni riutilizzabili. |
| [`test/`](test/) | Test C, Rust e di integrazione, organizzati secondo i moduli sorgenti. |
| [`scripts/`](scripts/) | Strumenti comuni di compilazione, test e gestione delle campagne QEMU. |
| [`docs/`](docs/) | Stato del progetto, architettura, istruzioni e risultati di validazione. |

Gli script specifici dei moduli si trovano nelle rispettive sottocartelle
`scripts/`. Gli artefatti generati nelle directory `build/` sono esclusi
dal versionamento.

---

## Librerie comuni e codice di piattaforma

Le funzioni riutilizzate da più moduli vivono in [`lib/`](lib/).
Per esempio, [`lib/security/`](lib/security/) raccoglie primitive comuni
come SHA-256 e HMAC. Il codice che dipende dalla piattaforma vive in
[`arch/x86/`](arch/x86/), dove [`arch/x86/mm/`](arch/x86/mm/) gestisce le
tabelle di paging e le mappature x86.

---

## Licenza

VoidOS è distribuito sotto la **GNU General Public License, versione 3**
(GPL-3.0-only). Il testo completo è disponibile in [LICENSE](LICENSE).
Eventuali componenti di terze parti conservano le proprie licenze.
