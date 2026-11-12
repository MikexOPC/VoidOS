# Architettura di VoidOS

Il loader UEFI carica il kernel ELF, prepara le informazioni di avvio ed
esce dai servizi di boot. Il kernel verifica il contratto, installa i propri
descrittori e stack, prende possesso della memoria e inizializza CPU e
dispositivi. Lo [stato corrente](status.md) indica quali percorsi sono
attivi nell'immagine ordinaria e quali richiedono altri profili.

| Area | Dove cercare |
| --- | --- |
| Loader, ELF del kernel, passaggio da UEFI | [arch/x86/boot/uefi/](../arch/x86/boot/uefi/) |
| Contratto e sequenza kernel | [kernel/boot/](../kernel/boot/), [kernel/main.c](../kernel/main.c), [kernel/startup/](../kernel/startup/) |
| Layout x86, paging e inventario | [layout.h](../arch/x86/configs/layout.h), [arch/x86/mm/](../arch/x86/mm/) |
| Bootstrap, pool fisico, heap e oggetti | [kernel/bootstrap/](../kernel/bootstrap/), [kernel/mm/](../kernel/mm/) |
| ACPI, CPU, avvio degli AP e IPI | [kernel/firmware/acpi/](../kernel/firmware/acpi/), [arch/x86/kernel/cpu/percpu/](../arch/x86/kernel/cpu/percpu/) |
| Eccezioni, IRQ, descrittori e timer | [exceptions/](../arch/x86/kernel/exceptions/), [interrupts/](../arch/x86/kernel/interrupts/), [descriptors/](../arch/x86/kernel/descriptors/), [timer/](../arch/x86/kernel/timer/) sotto `arch/x86/kernel/` |
| Lock e scheduler | [kernel/sync/](../kernel/sync/), [kernel/scheduler/](../kernel/scheduler/) e [backend x86](../arch/x86/kernel/scheduler/) |
| Processo, loader utente e syscall | [kernel/process/](../kernel/process/), [arch/x86/kernel/process/](../arch/x86/kernel/process/), [userspace/init/](../userspace/init/) |
| Bridge | [bridge/](../bridge/) per contratto e servizio, [test/bridge/](../test/bridge/) per le prove host |
| Driver, console e shell | [kernel/drivers/](../kernel/drivers/), [backend x86](../arch/x86/kernel/drivers/), [kernel/console/](../kernel/console/), [kernel/shell/](../kernel/shell/) |
| Primitive riutilizzabili | [lib/](../lib/) e [librerie x86](../arch/x86/lib/) |
| Build e prove | [kernel/meson.build](../kernel/meson.build), [scripts/tooling/](../scripts/tooling/), [registro QEMU](../scripts/test/campaigns.json), [test/](../test/) |

L'interfaccia fra codice comune e backend hardware appartiene al kernel.
I backend x86 gestiscono registri, paging e transizioni di privilegio.
C collega le risorse e le interfacce, Rust gestisce diversi validatori e
macchine a stati, Assembly gestisce gli ingressi CPU e gli switch.

## Design e validazione

| Obiettivo | Design | Validazione |
| --- | --- | --- |
| Consolidamento | [Build, layout e SMP](design/consolidamento.md) | [Boot, scheduler e build](validation/consolidamento.md) |
| Processi | [Caricamento, syscall e IPC](design/processi.md) | [K5, K6 e IPC](validation/processi.md) |
| Sicurezza | [Protezioni ed entropia](design/sicurezza.md) | [Protezioni CPU e primitive host](validation/sicurezza.md) |

Storage, esecuzione da file e GUI hanno i propri obiettivi nella
[roadmap](roadmap.md). La [direzione del progetto](decisions.md) raccoglie
le scelte già prese. Il [registro del debito](future-work.md) contiene
anche voci storiche datate.
